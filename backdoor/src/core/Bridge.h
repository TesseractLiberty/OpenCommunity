#pragma once

#include "../config/ModuleConfig.h"

class Bridge {
public:
    Bridge();
    ~Bridge();

    bool Initialize();
    void Shutdown();

    ModuleConfig* GetConfig() { return m_Config; }
    bool IsReady() const { return m_Initialized; }

    static Bridge* Get() {
        static Bridge instance;
        return &instance;
    }

private:
    HANDLE m_MapFile = nullptr;
    ModuleConfig* m_Config = nullptr;
    bool m_Initialized = false;

    static constexpr const wchar_t* MEMORY_NAME = L"OpenCommunitySharedMem";
};
