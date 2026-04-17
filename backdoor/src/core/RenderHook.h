#pragma once

#include <mutex>
#include <vector>

namespace RenderCache {
    extern std::vector<float> modelView;
    extern std::vector<float> projection;
    extern int viewportW;
    extern int viewportH;
    extern std::mutex mtx;
    extern bool captured;
}

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
