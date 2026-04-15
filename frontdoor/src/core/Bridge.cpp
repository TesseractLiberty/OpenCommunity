#include "pch.h"
#include "Bridge.h"

Bridge::Bridge() = default;

Bridge::~Bridge() {
    Shutdown();
}

bool Bridge::Initialize() {
    if (m_Initialized) return true;

    m_MapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(ModuleConfig),
        MEMORY_NAME
    );
    
    if (!m_MapFile) {
        m_MapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MEMORY_NAME);
        if (!m_MapFile) {
            return false;
        }
    }
    
    m_Config = static_cast<ModuleConfig*>(MapViewOfFile(
        m_MapFile,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(ModuleConfig)
    ));
    
    if (!m_Config) {
        CloseHandle(m_MapFile);
        m_MapFile = nullptr;
        return false;
    }
    
    static bool firstInit = true;
    if (firstInit) {
        ZeroMemory(m_Config, sizeof(ModuleConfig));
        m_Config->Reset();
        firstInit = false;
    }
    
    m_Initialized = true;
    return true;
}

void Bridge::Shutdown() {
    if (m_Config) {
        UnmapViewOfFile(m_Config);
        m_Config = nullptr;
    }
    
    if (m_MapFile) {
        CloseHandle(m_MapFile);
        m_MapFile = nullptr;
    }
    
    m_Initialized = false;
}
