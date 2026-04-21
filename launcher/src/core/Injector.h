#pragma once

#include <windows.h>
#include <vector>
#include <string>

class Injector {
public:
    struct MappingData {
        LPVOID BaseAddress;
        HMODULE(WINAPI* LoadLibraryAFn)(LPCSTR);
        FARPROC(WINAPI* GetProcAddressFn)(HMODULE, LPCSTR);
    };

    Injector() = default;
    ~Injector() { Unload(); }

    bool InjectFromMemory(DWORD pid, const std::vector<uint8_t>& dllData);
    bool InjectFromFile(DWORD pid, const std::wstring& filePath);
    void Unload();
    bool IsInjected() const { return m_BaseAddress != 0; }

    static Injector* Get() {
        static Injector instance;
        return &instance;
    }

private:
    bool ManualMap(HANDLE hProcess, const std::vector<uint8_t>& dllData);
    void Obfuscate(std::vector<uint8_t>& data);
    void Deobfuscate(std::vector<uint8_t>& data);

    HANDLE m_TargetProcess = nullptr;
    uintptr_t m_BaseAddress = 0;
    size_t m_ImageSize = 0;
    std::vector<uint8_t> m_StoredDll;
};
