#pragma once

class RenderHook {
public:
    bool Initialize();
    void Shutdown();
    void OnRender(HDC hdc);

    static RenderHook* Get() {
        static RenderHook instance;
        return &instance;
    }

private:
    static LONG CALLBACK VehHandler(PEXCEPTION_POINTERS pExInfo);

    void* m_TargetAddress = nullptr;
    void* m_VehHandle = nullptr;
    uint8_t m_OriginalByte = 0;
    bool m_Installed = false;
    bool m_SingleStepping = false;
};
