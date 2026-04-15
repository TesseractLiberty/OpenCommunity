#include "pch.h"
#include "features/render/HUD.h"
#include "features/combat/AutoClicker.h"
#include "core/Bridge.h"
#include "core/RenderHook.h"

DWORD WINAPI MainThread(LPVOID param) {
    __try {
        Sleep(3000);
        
        if (!Bridge::Get()->Initialize()) {
            return 1;
        }
        
        RenderHook::Get()->Initialize();
        
        auto* config = Bridge::Get()->GetConfig();
        while (config && !config->m_Destruct) {
            __try {
                AutoClicker::Get()->Run();
                config->Modules.m_AutoClicker = config->AutoClicker.m_Enabled;
            } __except(EXCEPTION_EXECUTE_HANDLER) {
            }
            Sleep(1);
        }
        
        RenderHook::Get()->Shutdown();
        Bridge::Get()->Shutdown();
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
