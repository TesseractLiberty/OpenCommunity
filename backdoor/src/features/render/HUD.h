#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

struct ImFont;
struct ModuleConfig;

class HUD {
public:
    HUD() = default;
    ~HUD() = default;

    void Render(ModuleConfig* config, float screenW, float screenH);
    void SetFonts(ImFont* regular, ImFont* bold, ImFont* vape = nullptr);

    static HUD* Get() {
        static HUD instance;
        return &instance;
    }

private:
    struct ModuleEntry {
        std::string name;
        std::string tag;
        float width;
        bool inUse = false;
    };

    std::vector<ModuleEntry> GetActiveModules(ImFont* nameFont, float nameFontSize, ImFont* tagFont, float tagFontSize);
    float DrawFormattedTag(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& pos, const std::string& text, ImU32 defaultColor, ImU32 shadowColor);
    float CalcFormattedTagWidth(ImFont* font, float fontSize, const std::string& text) const;
    void GetWaveRGB(const ModuleConfig* config, int offset, float& r, float& g, float& b);
    void GetRiseRGB(int offset, float& r, float& g, float& b);
    void GetVapeV4RGB(int offset, float& r, float& g, float& b);
    void GetTesseractRGB(int offset, float& r, float& g, float& b);
    void GetTesseractHeaderRGB(int offset, float& r, float& g, float& b);

    int m_Fps = 0;
    int m_FrameCount = 0;
    std::chrono::steady_clock::time_point m_LastFpsTime = std::chrono::steady_clock::now();
    std::unordered_map<std::string, float> m_SlideProgress;
    std::chrono::steady_clock::time_point m_LastFrameTime = std::chrono::steady_clock::now();
    ImFont* m_RegularFont = nullptr;
    ImFont* m_BoldFont = nullptr;
    ImFont* m_VapeFont = nullptr;
};
