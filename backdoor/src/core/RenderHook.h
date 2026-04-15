#pragma once

class RenderHook {
public:
    bool Initialize();
    void Shutdown();

    static RenderHook* Get() {
        static RenderHook instance;
        return &instance;
    }

private:
    bool m_Installed = false;
    bool m_ImGuiInitialized = false;
};
