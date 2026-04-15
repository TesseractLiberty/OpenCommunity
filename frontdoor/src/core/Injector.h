#pragma once

#include <blackbone/Process/Process.h>

class Injector {
public:
    Injector() = default;
    ~Injector() { Unload(); }

    bool InjectFromMemory(DWORD pid, const std::vector<uint8_t>& dllData);
    bool InjectFromFile(DWORD pid, const std::wstring& filePath);
    void Unload();
    bool IsInjected() const { return !m_MappedModules.empty(); }

    static Injector* Get() {
        static Injector instance;
        return &instance;
    }

private:
    bool InjectBuffer(DWORD pid, void* buffer, size_t size);
    void Obfuscate(std::vector<uint8_t>& data);
    void Deobfuscate(std::vector<uint8_t>& data);

    blackbone::Process m_Process;

    struct MappedModule {
        uint64_t base;
        size_t size;
    };
    std::vector<MappedModule> m_MappedModules;
    std::vector<uint8_t> m_StoredDll;
};
