#include "pch.h"
#include "ui/Screen.h"
#include "core/Bridge.h"
#include "core/Injector.h"
#include "config/ClientInfo.h"
#include "../../shared/common/ModuleConfig.h"

#include <shellapi.h>

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

    const bool result = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) != FALSE;
    CloseHandle(token);

    return result;
}

void MonitorGameThread() {
    while (true) {
        auto* info = Singleton<ClientInfo>::Get();
        auto* config = Bridge::Get()->GetConfig();

        if (!info || !config) {
            Sleep(1000);
            continue;
        }

        if (config->m_Destruct) {
            break;
        }

        if (info->m_TargetPid != 0) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info->m_TargetPid);
            if (hProcess) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                    config->m_Destruct = true;
                    CloseHandle(hProcess);
                    break;
                }
                CloseHandle(hProcess);
            } else {
                config->m_Destruct = true;
                break;
            }
        }

        Sleep(1000);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    srand(static_cast<unsigned>(time(nullptr)));
    EnableDebugPrivilege();

    int argumentCount = 0;
    LPWSTR* argumentVector = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    bool enemyInfoWindow = false;
    if (argumentVector) {
        for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex) {
            if (_wcsicmp(argumentVector[argumentIndex], L"--enemy-info-window") == 0) {
                enemyInfoWindow = true;
                break;
            }
        }

        LocalFree(argumentVector);
    }

    if (!Bridge::Get()->Initialize()) {
        MessageBoxA(nullptr, "failed to initialize communication", "error", MB_OK | MB_ICONERROR);
        return 1;
    }

    auto* info = Singleton<ClientInfo>::Get();
    info->m_Width = enemyInfoWindow ? 920.0f : 900.0f;
    info->m_Height = enemyInfoWindow ? 900.0f : 550.0f;

    if (!enemyInfoWindow) {
        std::thread monitorThread(MonitorGameThread);
        monitorThread.detach();
    }

    {
        Screen screen(info->m_Width, info->m_Height, enemyInfoWindow);
        screen.Run();
    }

    if (!enemyInfoWindow) {
        Injector::Get()->Unload();
    }

    Bridge::Get()->Shutdown();
    return 0;
}
