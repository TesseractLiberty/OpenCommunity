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

namespace {
    const char* VersionToString(GameVersions version) {
        switch (version) {
        case BADLION:
            return "BADLION";
        case FORGE_1_8:
            return "FORGE_1_8";
        case FEATHER_1_8:
            return "FEATHER_1_8";
        case LUNAR:
            return "LUNAR";
        default:
            return "UNKNOWN";
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

static DWORD MainThreadImpl() {
    Sleep(3000);
    OC_LOG_INFO("Runtime", "Main thread started.");
    
    if (!Bridge::Get()->Initialize()) {
        OC_LOG_ERROR("Runtime", "Bridge initialization failed.");
        return 1;
    }
    OC_LOG_INFO("Runtime", "Bridge initialized.");
    
    g_Game = new GameInstance();
    if (!g_Game->Attach()) {
        OC_LOG_ERROR("Runtime", "Failed to attach to JVM/JVMTI.");
        delete g_Game;
        g_Game = nullptr;
    }
    else {
        OC_LOG_INFO("Runtime", "Attached to JVM/JVMTI.");
    }
    
    if (g_Game && !g_Game->InitializeGame()) {
        OC_LOG_ERROR("Runtime", "Failed to initialize game instance.");
        g_Game->Detach();
        delete g_Game;
        g_Game = nullptr;
    }
    else if (g_Game) {
        OC_LOG_INFOF("Runtime", "Game initialized with version %s.", VersionToString(g_Game->GetGameVersion()));
    }
    
    ModuleRegistry::RegisterAll();
    
    MH_Initialize();
    const bool gameThreadHookInitialized = GameThreadHook::Initialize();
    OC_LOG_INFOF("Runtime", "GameThreadHook initialize result: %s", gameThreadHookInitialized ? "success" : "failed");
    RenderHook::Get()->Initialize();
    
    auto* config = Bridge::Get()->GetConfig();
    auto* modules = ModuleManager::Get();
    modules->SetModuleToggleCallback([](const Module& module, bool enabled) {
        if (enabled) {
            Notifications::SendNotifications::ENABLED(module.GetName());
        } else {
            Notifications::SendNotifications::DISABLED(module.GetName());
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
        break;
    }
    
    return TRUE;
}
