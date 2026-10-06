#pragma once

#include "../../../shared/common/ModuleConfig.h"
#include "../../../shared/common/security/XorString.h"

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

    // Nome do file-mapping decriptado de forma transiente (ring thread_local).
    // Nunca em plaintext em .rdata: o literal vive cifrado via XOR_W.
    static const wchar_t* MemoryName() { return XOR_W(L"OpenCommunitySharedMem"); }
};
