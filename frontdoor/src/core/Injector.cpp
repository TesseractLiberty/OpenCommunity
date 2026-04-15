#include "pch.h"
#include "Injector.h"

void Injector::Obfuscate(std::vector<uint8_t>& data) {
    const uint8_t xorKey = 0x69;
    const int shift = 7;
    
    for (auto& byte : data) {
        byte = (byte ^ xorKey) + shift;
    }
}

void Injector::Deobfuscate(std::vector<uint8_t>& data) {
    const uint8_t xorKey = 0x69;
    const int shift = 7;
    
    for (auto& byte : data) {
        byte = (byte - shift) ^ xorKey;
    }
}

bool Injector::InjectBuffer(DWORD pid, void* buffer, size_t size) {
    if (!NT_SUCCESS(m_Process.Attach(pid, PROCESS_ALL_ACCESS))) {
        return false;
    }
    
    if (size < 0x1000) {
        return false;
    }

    auto image = m_Process.mmap().MapImage(
        static_cast<size_t>(size),
        buffer,
        false,
        blackbone::NoTLS | blackbone::WipeHeader
    );
    
    if (image.success()) {
        m_MappedModules.push_back({
            image.result()->baseAddress,
            image.result()->size
        });
        return true;
    }
    
    return false;
}

bool Injector::InjectFromMemory(DWORD pid, const std::vector<uint8_t>& dllData) {
    if (dllData.empty()) return false;

    std::vector<uint8_t> data = dllData;
    Deobfuscate(data);

    void* buffer = VirtualAlloc(nullptr, data.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer) {
        Obfuscate(data);
        return false;
    }

    memcpy(buffer, data.data(), data.size());
    bool result = InjectBuffer(pid, buffer, data.size());

    SecureZeroMemory(buffer, data.size());
    VirtualFree(buffer, 0, MEM_RELEASE);

    Obfuscate(data);
    
    return result;
}

bool Injector::InjectFromFile(DWORD pid, const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return false;
    }
    file.close();
    
    Obfuscate(data);
    m_StoredDll = data;

    return InjectFromMemory(pid, data);
}

void Injector::Unload() {
    for (const auto& mod : m_MappedModules) {
        std::vector<uint8_t> zeros(mod.size, 0);
        m_Process.memory().Write(mod.base, mod.size, zeros.data());
    }
    m_MappedModules.clear();

    if (NT_SUCCESS(m_Process.mmap().UnmapAllModules())) {
        m_Process.mmap().Cleanup();
    }

    if (!m_StoredDll.empty()) {
        SecureZeroMemory(m_StoredDll.data(), m_StoredDll.size());
        m_StoredDll.clear();
    }
}
