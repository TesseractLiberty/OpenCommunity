#include "pch.h"
#include "features/render/HUD.h"
#include "core/Bridge.h"
#include "core/GL11Hook.h"
#include "core/RenderHook.h"
#include "core/GameThreadHook.h"
#include "game/jni/GameInstance.h"
#include "../../shared/common/RegisterModules.h"
#include "../../deps/minhook/MinHook.h"

static void SafeUpdateModules(FeatureManager* fm, void* config) {
    __try {
        fm->UpdateBackdoor(config);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void SafeTickSynchronousFallback(FeatureManager* fm) {
    __try {
        if (!g_Game || !g_Game->IsInitialized()) {
            return;
        }

        auto* env = g_Game->GetCurrentEnv();
        if (!env || env->PushLocalFrame(256) != 0) {
            return;
        }

        fm->TickSynchronousAll(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->PopLocalFrame(nullptr);
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
    
    RegisterAllModules();
    
    MH_Initialize();
    GameThreadHook::Initialize();
    GL11Hook::Initialize();
    RenderHook::Get()->Initialize();
    
    auto* config = Bridge::Get()->GetConfig();
    auto* fm = FeatureManager::Get();

    while (config && !config->m_Destruct) {
        SafeUpdateModules(fm, config);
        if (GameThreadHook::ShouldRunFallback()) {
            SafeTickSynchronousFallback(fm);
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
