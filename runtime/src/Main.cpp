#include "pch.h"
#include "features/render/HUD.h"
#include "core/Bridge.h"
#include "core/RenderHook.h"
#include "core/GameThreadHook.h"
#include "features/ModuleRegistry.h"
#include "features/visuals/Notifications.h"
#include "game/jni/GameInstance.h"
#include "game/jni/JniRefs.h"
#include "../../shared/common/logging/Logger.h"
#include "../../deps/minhook/MinHook.h"
#include "../../deps/imgui/imgui_internal.h"

namespace {
    const char* VersionToString(GameVersions version) {
        switch (version) {
        case BADLION:
            return XOR("BADLION");
        case FORGE_1_8:
            return XOR("FORGE_1_8");
        case FEATHER_1_8:
            return XOR("FEATHER_1_8");
        case LUNAR:
            return XOR("LUNAR");
        default:
            return XOR("UNKNOWN");
        }
    }
}

static void SafeUpdateModules(ModuleManager* modules, void* config) {
    __try {
        modules->UpdateRuntime(config);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void TickSynchronousFallback(ModuleManager* modules) {
    if (!g_Game || !g_Game->IsInitialized()) {
        return;
    }

    auto* env = g_Game->GetCurrentEnv();
    JniLocalFrame frame(env, 256);
    if (!frame.IsActive()) {
        return;
    }

    GameThreadHook::SanitizeInteractionState(env);
    modules->TickSynchronousAll(env);
    GameThreadHook::SanitizeInteractionState(env);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

static void SafeTickSynchronousFallback(ModuleManager* modules) {
    __try {
        TickSynchronousFallback(modules);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void SafeShutdownRuntimeAll(ModuleManager* modules, void* env) {
    __try {
        modules->ShutdownRuntimeAll(env);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void ScrubRuntimeUiTextState() {
    __try {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (!ctx) return;
        for (ImGuiWindow* w : ctx->Windows) {
            if (w && w->Name && w->NameBufLen > 0)
                MemoryScrub::Wipe(w->Name, static_cast<size_t>(w->NameBufLen));
        }
        for (ImGuiWindow* w : ctx->WindowsFocusOrder) {
            if (w && w->Name && w->NameBufLen > 0)
                MemoryScrub::Wipe(w->Name, static_cast<size_t>(w->NameBufLen));
        }
        if (ctx->InputTextState.TextA.Data && ctx->InputTextState.TextA.Capacity > 0)
            MemoryScrub::Wipe(ctx->InputTextState.TextA.Data, static_cast<size_t>(ctx->InputTextState.TextA.Capacity));
        if (ctx->InputTextState.InitialTextA.Data && ctx->InputTextState.InitialTextA.Capacity > 0)
            MemoryScrub::Wipe(ctx->InputTextState.InitialTextA.Data, static_cast<size_t>(ctx->InputTextState.InitialTextA.Capacity));
        if (ctx->SettingsWindows.Buf.Data && ctx->SettingsWindows.Buf.Capacity > 0)
            MemoryScrub::Wipe(ctx->SettingsWindows.Buf.Data, static_cast<size_t>(ctx->SettingsWindows.Buf.Capacity));
        ctx->SettingsWindows.clear();
        if (ctx->SettingsIniData.Buf.Data && ctx->SettingsIniData.Buf.Capacity > 0)
            MemoryScrub::Wipe(ctx->SettingsIniData.Buf.Data, static_cast<size_t>(ctx->SettingsIniData.Buf.Capacity));
        ctx->SettingsIniData.clear();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
}

static DWORD MainThreadImpl() {
    Sleep(3000);
    OC_LOG_INFO(XOR("Runtime"), XOR("Main thread started."));

    if (!Bridge::Get()->Initialize()) {
        OC_LOG_ERROR(XOR("Runtime"), XOR("Bridge initialization failed."));
        return 1;
    }
    OC_LOG_INFO(XOR("Runtime"), XOR("Bridge initialized."));

    g_Game = new GameInstance();
    if (!g_Game->Attach()) {
        OC_LOG_ERROR(XOR("Runtime"), XOR("Failed to attach to JVM/JVMTI."));
        delete g_Game;
        g_Game = nullptr;
    }
    else {
        OC_LOG_INFO(XOR("Runtime"), XOR("Attached to JVM/JVMTI."));
    }

    if (g_Game && !g_Game->InitializeGame()) {
        OC_LOG_ERROR(XOR("Runtime"), XOR("Failed to initialize game instance."));
        g_Game->Detach();
        delete g_Game;
        g_Game = nullptr;
    }
    else if (g_Game) {
        OC_LOG_INFOF(XOR("Runtime"), XOR("Game initialized with version %s."), VersionToString(g_Game->GetGameVersion()));
    }

    ModuleRegistry::RegisterAll();

    MH_Initialize();
    const bool gameThreadHookInitialized = GameThreadHook::Initialize();
    OC_LOG_INFOF(XOR("Runtime"), XOR("GameThreadHook initialize result: %s"), gameThreadHookInitialized ? XOR("success") : XOR("failed"));
    RenderHook::Get()->Initialize();
    
    auto* config = Bridge::Get()->GetConfig();
    auto* modules = ModuleManager::Get();
    modules->SetModuleToggleCallback([](const Module& module, bool enabled) {
        if (enabled) {
            Notifications::SendNotifications::ENABLED(module);
        } else {
            Notifications::SendNotifications::DISABLED(module);
        }
    });

    while (config && !config->m_Destruct) {
        SafeUpdateModules(modules, config);
        if (GameThreadHook::ShouldRunFallback()) {
            SafeTickSynchronousFallback(modules);
        }
        Sleep(1);
    }

    if (g_Game && g_Game->IsInitialized()) {
        if (auto* env = g_Game->GetCurrentEnv()) {
            SafeShutdownRuntimeAll(modules, env);
        }
    }
    
    RenderHook::Get()->Shutdown();
    GameThreadHook::Shutdown();
    MH_Uninitialize();
    
    if (g_Game) {
        g_Game->Detach();
        delete g_Game;
        g_Game = nullptr;
    }
    
    Bridge::Get()->Shutdown();
    
    return 0;
}

DWORD WINAPI MainThread(LPVOID param) {
    __try {
        return MainThreadImpl();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
    
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)lpReserved;
    
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        {
            HANDLE thread = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
            if (thread) {
                CloseHandle(thread);
            }
        }
        break;
        
    case DLL_PROCESS_DETACH:
        ScrubRuntimeUiTextState();
        if (ModuleManager::Get()) ModuleManager::Get()->ScrubAllTextBuffers();
        Notifications::ScrubNotifications();
        string_obfuscation::WipeAllXorStrings();
        break;
    }
    
    return TRUE;
}
