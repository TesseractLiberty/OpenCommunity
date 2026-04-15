#include "pch.h"
#include "modules/UserInterface.h"
#include "impl/Communication.h"
#include "impl/InjectionHelper.h"
#include "saves/Globals.h"
#include "saves/Settings.h"

bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    LUID luid;
    TOKEN_PRIVILEGES tp;
    
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        CloseHandle(token);
        return false;
    }
    
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    
    bool result = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(token);
    
    return result;
}

void MonitorGameThread() {
    while (true) {
        auto* globals = Singleton<Globals>::Get();
        auto* settings = Communication::Get()->GetSettings();
        
        if (!globals || !settings) {
            Sleep(1000);
            continue;
        }
        
        if (settings->m_Destruct) {
            break;
        }
        
        if (globals->m_TargetPid != 0) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, globals->m_TargetPid);
            if (hProcess) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(hProcess, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        settings->m_Destruct = true;
                        CloseHandle(hProcess);
                        break;
                    }
                }
                CloseHandle(hProcess);
            } else {
                settings->m_Destruct = true;
                break;
            }
        }
        
        Sleep(1000);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    
    srand(static_cast<unsigned>(time(nullptr)));
    EnableDebugPrivilege();
    
    if (!Communication::Get()->Initialize()) {
        MessageBoxA(nullptr, "falha ao iniciar comunicação", "erro", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    auto* globals = Singleton<Globals>::Get();
    globals->m_Width = 900.0f;
    globals->m_Height = 550.0f;
    
    std::thread monitorThread(MonitorGameThread);
    monitorThread.detach();
    {
        UserInterface ui(globals->m_Width, globals->m_Height);
        ui.Run();
    }
    
    InjectionHelper::Get()->Unload();
    Communication::Get()->Shutdown();
    
    return 0;
}
