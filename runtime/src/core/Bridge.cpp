#include "pch.h"
#include "Bridge.h"
#include "../../../shared/common/logging/Logger.h"

Bridge::Bridge() = default;

Bridge::~Bridge() {
    Shutdown();
}

bool Bridge::Initialize() {
    if (m_Initialized) return true;

    m_MapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MEMORY_NAME);
    if (!m_MapFile) {
        OC_LOG_WARNINGF("Bridge", "OpenFileMappingW failed with %lu, retrying.", GetLastError());
        Sleep(1000);
        m_MapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MEMORY_NAME);
        if (!m_MapFile) {
            OC_LOG_ERRORF("Bridge", "OpenFileMappingW retry failed with %lu.", GetLastError());
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
        OC_LOG_ERRORF("Bridge", "MapViewOfFile failed with %lu.", GetLastError());
        CloseHandle(m_MapFile);
        m_MapFile = nullptr;
        return false;
    }

    if (!m_Config->IsCompatible()) {
        OC_LOG_ERRORF(
            "Bridge",
            "Incompatible ModuleConfig schema. magic=0x%08X version=%u size=%u expectedSize=%zu.",
            m_Config->m_Magic,
            m_Config->m_Version,
            m_Config->m_Size,
            sizeof(ModuleConfig));
        Shutdown();
        return false;
    }
    
    m_Initialized = true;
    OC_LOG_INFO("Bridge", "Shared memory bridge attached.");
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
