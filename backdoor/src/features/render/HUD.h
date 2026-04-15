#pragma once

#include <vector>
#include <string>
#include <chrono>

struct ModuleConfig;

class HUD {
public:
    HUD() = default;
    ~HUD();

    void Render(HDC hdc, ModuleConfig* config);

    static HUD* Get() {
        static HUD instance;
        return &instance;
    }

private:
    struct ModuleEntry {
        const char* name;
        const char* mode;
        float width;
        bool enabled;
    };

    std::vector<ModuleEntry> GetActiveModules();
    void InitFont(HDC hdc);
    void CleanupFont();
    void DrawRect(float x, float y, float w, float h, float r, float g, float b, float a);
    void DrawRoundedRect(float x, float y, float w, float h, float radius, float r, float g, float b, float a);
    void DrawText(float x, float y, const char* text, float r, float g, float b);
    int MeasureText(const char* text);
    void GetRainbowRGB(int offset, float& r, float& g, float& b);

    bool m_FontInitialized = false;
    unsigned int m_FontBase = 0;
    int m_CharWidths[256] = {};
    int m_FontHeight = 14;
    HDC m_FontDC = nullptr;
    HANDLE m_FontMemHandle = nullptr;
    bool m_FontResourceLoaded = false;
    int m_ScreenW = 1920;
    int m_ScreenH = 1080;
};
