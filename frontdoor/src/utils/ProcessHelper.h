#pragma once

namespace proc {
    inline DWORD FindProcessId(const wchar_t* processName) {
        DWORD pid = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, processName) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe));
        }

        CloseHandle(snapshot);
        return pid;
    }

    inline bool IsProcessRunning(DWORD pid) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess) return false;

        DWORD exitCode = 0;
        BOOL result = GetExitCodeProcess(hProcess, &exitCode);
        CloseHandle(hProcess);

        return result && exitCode == STILL_ACTIVE;
    }

    inline DWORD WaitForProcess(const wchar_t* processName, DWORD timeoutMs = 30000) {
        auto start = GetTickCount();
        while (GetTickCount() - start < timeoutMs) {
            DWORD pid = FindProcessId(processName);
            if (pid != 0) return pid;
            Sleep(500);
        }
        return 0;
    }
}
