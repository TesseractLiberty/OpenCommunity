#include "pch.h"
#include "Bridge.h"
#include "../../../shared/common/logging/Logger.h"

Bridge::Bridge() = default;

Bridge::~Bridge() {
    Shutdown();
}

bool Bridge::Initialize() {
    if (m_Initialized) return true;

    bool createdMapping = false;
    m_MapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(ModuleConfig),
        MEMORY_NAME
    );
    
    if (m_MapFile) {
        const DWORD createStatus = GetLastError();
        createdMapping = createStatus != ERROR_ALREADY_EXISTS;
    } else {
        const DWORD createError = GetLastError();
        OC_LOG_WARNINGF("Bridge", "CreateFileMappingW failed with %lu, trying OpenFileMappingW.", createError);

        m_MapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MEMORY_NAME);
        if (!m_MapFile) {
            OC_LOG_ERRORF("Bridge", "OpenFileMappingW failed with %lu.", GetLastError());
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
    
    if (createdMapping || !m_Config->IsCompatible()) {
        OC_LOG_INFO("Bridge", "Initializing shared ModuleConfig schema.");
        m_Config->Reset();
    }
    
    m_Initialized = true;
    OC_LOG_INFO("Bridge", "Shared memory bridge initialized.");
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
