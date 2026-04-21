#include "pch.h"
#include "features/render/HUD.h"
#include "core/Bridge.h"
#include "core/RenderHook.h"
#include "core/GameThreadHook.h"
#include "features/ModuleRegistry.h"
#include "features/visuals/Notifications.h"
#include "game/jni/GameInstance.h"
#include "game/jni/JniRefs.h"
#include "../../deps/minhook/MinHook.h"

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

static DWORD MainThreadImpl() {
    Sleep(3000);
    
    if (!Bridge::Get()->Initialize()) {
        return 1;
    }
    
    g_Game = new GameInstance();
    if (!g_Game->Attach()) {
        delete g_Game;
        g_Game = nullptr;
    }
    
    if (g_Game && !g_Game->InitializeGame()) {
        g_Game->Detach();
        delete g_Game;
        g_Game = nullptr;
    }
    
    ModuleRegistry::RegisterAll();
    
    MH_Initialize();
    GameThreadHook::Initialize();
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
