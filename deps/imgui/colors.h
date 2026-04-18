#pragma once

#include "imgui.h"
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace color
{
    inline bool contrastThemeEnabled = false;
    struct ThemePalette
    {
        ImVec4 background;
        ImVec4 strongText;
        ImVec4 moduleText;
        ImVec4 moduleAltText;
        ImVec4 glassSoft;
        ImVec4 glassStrong;
        ImVec4 glassHighlight;
        ImVec4 glassShadow;
        ImVec4 secondaryText;
        ImVec4 mutedText;
        ImVec4 panel;
        ImVec4 panelHover;
        ImVec4 panelActive;
        ImVec4 sidebar;
        ImVec4 fieldBg;
        ImVec4 fieldHover;
        ImVec4 fieldActive;
        ImVec4 border;
        ImVec4 link;
        ImVec4 linkHover;
        ImVec4 success;
        ImVec4 warning;
        ImVec4 danger;
        ImVec4 topographicLine;
        ImVec4 iconTint;
    };

    inline bool customThemePaletteEnabled = false;
    inline ThemePalette customThemePalette = {};

    inline ImVec4 MakeColor(int r, int g, int b, float alpha = 1.0f)
    {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
    }

    inline ImVec4 ThemeColor(const ImVec4& light, const ImVec4& dark)
    {
        return contrastThemeEnabled ? dark : light;
    }

    inline ImVec4 WithAlpha(const ImVec4& value, float alpha)
    {
        return ImVec4(value.x, value.y, value.z, alpha);
    }

    inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t, float alpha = -1.0f)
    {
        const float blend = fmaxf(0.0f, fminf(t, 1.0f));
        ImVec4 mixed(
            a.x + (b.x - a.x) * blend,
            a.y + (b.y - a.y) * blend,
            a.z + (b.z - a.z) * blend,
            a.w + (b.w - a.w) * blend
        );

        if (alpha >= 0.0f)
            mixed.w = alpha;

        return mixed;
    }

    inline ImVec4 ResolveThemeEntry(const ImVec4& light, const ImVec4& dark, ImVec4 ThemePalette::* member, float alpha)
    {
        const ImVec4 value = customThemePaletteEnabled ? (customThemePalette.*member) : ThemeColor(light, dark);
        return WithAlpha(value, alpha);
    }

    inline void SetContrastThemeEnabled(bool enabled)        { contrastThemeEnabled = enabled; }
    inline bool IsContrastThemeEnabled()                     { return contrastThemeEnabled; }
    inline void SetThemePalette(const ThemePalette& palette) { customThemePalette = palette; customThemePaletteEnabled = true; }
    inline void ClearThemePalette()                          { customThemePaletteEnabled = false; }
    inline bool HasThemePalette()                            { return customThemePaletteEnabled; }

    inline ImVec4 GetBackgroundVec4(float alpha = 1.0f)      { return ResolveThemeEntry(MakeColor(0xFF, 0xFF, 0xFF), MakeColor(0x0B, 0x0E, 0x13), &ThemePalette::background, alpha); }
    inline ImVec4 GetStrongTextVec4(float alpha = 1.0f)      { return ResolveThemeEntry(MakeColor(0x00, 0x00, 0x00), MakeColor(0xF5, 0xF7, 0xFB), &ThemePalette::strongText, alpha); }
    inline ImVec4 GetModuleTextVec4(float alpha = 1.0f)      { return ResolveThemeEntry(MakeColor(0x1A, 0x1A, 0x1A), MakeColor(0xE6, 0xEB, 0xF3), &ThemePalette::moduleText, alpha); }
    inline ImVec4 GetModuleAltTextVec4(float alpha = 1.0f)   { return ResolveThemeEntry(MakeColor(0x33, 0x33, 0x33), MakeColor(0xC7, 0xD0, 0xDC), &ThemePalette::moduleAltText, alpha); }
    inline ImVec4 GetGlassSoftVec4(float alpha = 1.0f)       { return ResolveThemeEntry(MakeColor(0xE0, 0xE0, 0xE0), MakeColor(0x17, 0x1B, 0x22), &ThemePalette::glassSoft, alpha); }
    inline ImVec4 GetGlassStrongVec4(float alpha = 1.0f)     { return ResolveThemeEntry(MakeColor(0xCC, 0xCC, 0xCC), MakeColor(0x1F, 0x25, 0x2E), &ThemePalette::glassStrong, alpha); }
    inline ImVec4 GetGlassHighlightVec4(float alpha = 1.0f)  { return ResolveThemeEntry(MakeColor(0xF5, 0xF5, 0xF5), MakeColor(0x2B, 0x32, 0x3E), &ThemePalette::glassHighlight, alpha); }
    inline ImVec4 GetGlassShadowVec4(float alpha = 1.0f)     { return ResolveThemeEntry(MakeColor(0x99, 0x99, 0x99), MakeColor(0x05, 0x07, 0x0B), &ThemePalette::glassShadow, alpha); }
    inline ImVec4 GetPrimaryTextVec4(float alpha = 1.0f)     { return GetStrongTextVec4(alpha); }
    inline ImVec4 GetSecondaryTextVec4(float alpha = 1.0f)   { return ResolveThemeEntry(MakeColor(0x44, 0x44, 0x44), MakeColor(0x9F, 0xAC, 0xBC), &ThemePalette::secondaryText, alpha); }
    inline ImVec4 GetMutedTextVec4(float alpha = 1.0f)       { return ResolveThemeEntry(MakeColor(0x77, 0x77, 0x77), MakeColor(0x73, 0x7F, 0x90), &ThemePalette::mutedText, alpha); }
    inline ImVec4 GetPanelVec4(float alpha = 1.0f)           { return ResolveThemeEntry(MakeColor(0xF0, 0xF0, 0xF0), MakeColor(0x15, 0x19, 0x20), &ThemePalette::panel, alpha); }
    inline ImVec4 GetPanelHoverVec4(float alpha = 1.0f)      { return ResolveThemeEntry(MakeColor(0xE8, 0xE8, 0xE8), MakeColor(0x1C, 0x22, 0x2C), &ThemePalette::panelHover, alpha); }
    inline ImVec4 GetPanelActiveVec4(float alpha = 1.0f)     { return ResolveThemeEntry(MakeColor(0xDD, 0xDD, 0xDD), MakeColor(0x26, 0x2E, 0x3A), &ThemePalette::panelActive, alpha); }
    inline ImVec4 GetSidebarVec4(float alpha = 1.0f)         { return ResolveThemeEntry(MakeColor(0xF5, 0xF5, 0xF5), MakeColor(0x10, 0x14, 0x1A), &ThemePalette::sidebar, alpha); }
    inline ImVec4 GetFieldBgVec4(float alpha = 1.0f)         { return ResolveThemeEntry(MakeColor(0xF8, 0xF8, 0xF8), MakeColor(0x11, 0x16, 0x1D), &ThemePalette::fieldBg, alpha); }
    inline ImVec4 GetFieldHoverVec4(float alpha = 1.0f)      { return ResolveThemeEntry(MakeColor(0xF0, 0xF0, 0xF0), MakeColor(0x17, 0x1D, 0x26), &ThemePalette::fieldHover, alpha); }
    inline ImVec4 GetFieldActiveVec4(float alpha = 1.0f)     { return ResolveThemeEntry(MakeColor(0xE8, 0xE8, 0xE8), MakeColor(0x20, 0x28, 0x34), &ThemePalette::fieldActive, alpha); }
    inline ImVec4 GetBorderVec4(float alpha = 1.0f)          { return ResolveThemeEntry(MakeColor(0xCC, 0xCC, 0xCC), MakeColor(0x41, 0x4C, 0x5D), &ThemePalette::border, alpha); }
    inline ImVec4 GetLinkVec4(float alpha = 1.0f)            { return ResolveThemeEntry(MakeColor(0x3A, 0x7B, 0xFF), MakeColor(0x8A, 0xBC, 0xFF), &ThemePalette::link, alpha); }
    inline ImVec4 GetLinkHoverVec4(float alpha = 1.0f)       { return ResolveThemeEntry(MakeColor(0x23, 0x62, 0xE4), MakeColor(0xA8, 0xD0, 0xFF), &ThemePalette::linkHover, alpha); }
    inline ImVec4 GetSuccessVec4(float alpha = 1.0f)         { return ResolveThemeEntry(MakeColor(0x42, 0x9D, 0x5D), MakeColor(0x79, 0xD3, 0x9A), &ThemePalette::success, alpha); }
    inline ImVec4 GetWarningVec4(float alpha = 1.0f)         { return ResolveThemeEntry(MakeColor(0xD4, 0x87, 0x2E), MakeColor(0xF0, 0xB8, 0x63), &ThemePalette::warning, alpha); }
    inline ImVec4 GetDangerVec4(float alpha = 1.0f)          { return ResolveThemeEntry(MakeColor(0xC4, 0x4F, 0x4F), MakeColor(0xEF, 0x88, 0x88), &ThemePalette::danger, alpha); }
    inline ImVec4 GetTopographicLineVec4(float alpha = 1.0f) { return ResolveThemeEntry(MakeColor(0xC8, 0xC8, 0xC8), MakeColor(0x4D, 0x59, 0x69), &ThemePalette::topographicLine, alpha); }
    inline ImVec4 GetIconTintVec4(float alpha = 1.0f)        { return ResolveThemeEntry(MakeColor(0x00, 0x00, 0x00), MakeColor(0xF5, 0xF7, 0xFB), &ThemePalette::iconTint, alpha); }
    inline ImVec4 GetTransparentVec4()                       { return ImVec4(0.0f, 0.0f, 0.0f, 0.0f); }

    inline ImVec4 GetCycleVec4(float t, float alpha = 1.0f)
    {
        const ImVec4 palette[] = {
            GetStrongTextVec4(alpha),
            GetModuleTextVec4(alpha),
            GetModuleAltTextVec4(alpha)
        };

        const float wrapped = t - floorf(t);
        const float segment = wrapped * 3.0f;
        const int index = static_cast<int>(segment) % 3;
        const float local_t = segment - floorf(segment);
        return Mix(palette[index], palette[(index + 1) % 3], local_t, alpha);
    }

    inline ImU32 ToU32(const ImVec4& value)                  { return ImGui::ColorConvertFloat4ToU32(value); }
    inline ImU32 GetBackgroundU32(float alpha = 1.0f)        { return ToU32(GetBackgroundVec4(alpha)); }
    inline ImU32 GetAccentU32(float alpha = 1.0f)            { return ToU32(GetStrongTextVec4(alpha)); }
    inline ImU32 GetStrongTextU32(float alpha = 1.0f)        { return ToU32(GetStrongTextVec4(alpha)); }
    inline ImU32 GetModuleTextU32(float alpha = 1.0f)        { return ToU32(GetModuleTextVec4(alpha)); }
    inline ImU32 GetModuleAltTextU32(float alpha = 1.0f)     { return ToU32(GetModuleAltTextVec4(alpha)); }
    inline ImU32 GetSecondaryTextU32(float alpha = 1.0f)     { return ToU32(GetSecondaryTextVec4(alpha)); }
    inline ImU32 GetMutedTextU32(float alpha = 1.0f)         { return ToU32(GetMutedTextVec4(alpha)); }
    inline ImU32 GetPanelU32(float alpha = 1.0f)             { return ToU32(GetPanelVec4(alpha)); }
    inline ImU32 GetPanelHoverU32(float alpha = 1.0f)        { return ToU32(GetPanelHoverVec4(alpha)); }
    inline ImU32 GetPanelActiveU32(float alpha = 1.0f)       { return ToU32(GetPanelActiveVec4(alpha)); }
    inline ImU32 GetBorderU32(float alpha = 1.0f)            { return ToU32(GetBorderVec4(alpha)); }
    inline ImU32 GetFieldBgU32(float alpha = 1.0f)           { return ToU32(GetFieldBgVec4(alpha)); }
    inline ImU32 GetFieldHoverU32(float alpha = 1.0f)        { return ToU32(GetFieldHoverVec4(alpha)); }
    inline ImU32 GetFieldActiveU32(float alpha = 1.0f)       { return ToU32(GetFieldActiveVec4(alpha)); }
    inline ImU32 GetGlassSoftU32(float alpha = 1.0f)         { return ToU32(GetGlassSoftVec4(alpha)); }
    inline ImU32 GetGlassStrongU32(float alpha = 1.0f)       { return ToU32(GetGlassStrongVec4(alpha)); }
    inline ImU32 GetGlassHighlightU32(float alpha = 1.0f)    { return ToU32(GetGlassHighlightVec4(alpha)); }
    inline ImU32 GetGlassShadowU32(float alpha = 1.0f)       { return ToU32(GetGlassShadowVec4(alpha)); }
    inline ImU32 GetLinkU32(float alpha = 1.0f)              { return ToU32(GetLinkVec4(alpha)); }
    inline ImU32 GetLinkHoverU32(float alpha = 1.0f)         { return ToU32(GetLinkHoverVec4(alpha)); }
    inline ImU32 GetSuccessU32(float alpha = 1.0f)           { return ToU32(GetSuccessVec4(alpha)); }
    inline ImU32 GetWarningU32(float alpha = 1.0f)           { return ToU32(GetWarningVec4(alpha)); }
    inline ImU32 GetDangerU32(float alpha = 1.0f)            { return ToU32(GetDangerVec4(alpha)); }
    inline ImU32 GetTopographicLineU32(float alpha = 1.0f)   { return ToU32(GetTopographicLineVec4(alpha)); }
    inline ImU32 GetIconTintU32(float alpha = 1.0f)          { return ToU32(GetIconTintVec4(alpha)); }
    inline ImU32 GetCycleU32(float t, float alpha = 1.0f)    { return ToU32(GetCycleVec4(t, alpha)); }

    static float purple[4] = { 0.0f, 0.0f, 0.0f, 1.f };
    static float customColor[4] = { 0.0f, 0.0f, 0.0f, 1.f };
    static bool rainbowModeEnabled = false;
    static float speed = 0.1f;
    static float targetColor[4] = { 0.0f, 0.0f, 0.0f, 1.f };
    static bool initialized = false;

    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    inline float RandomRange(float min, float max) { return min + static_cast<float>(rand()) / (RAND_MAX / (max - min)); }

    inline float ColorDistance(const float* a, const float* b)
    {
        float dist = 0.f;
        for (int i = 0; i < 3; ++i)
        {
            float d = a[i] - b[i];
            dist += d * d;
        }
        return std::sqrt(dist);
    }

    inline void GenerateNewTargetColor()
    {
        static const float palette[][4] = {
            { 0.0f, 0.0f, 0.0f, 1.f },
            { 0.1f, 0.1f, 0.1f, 1.f },
            { 0.2f, 0.2f, 0.2f, 1.f }
        };

        int newIndex = rand() % 3;
        for (int i = 0; i < 4; ++i)
            targetColor[i] = palette[newIndex][i];
    }

    inline float MoveTowards(float current, float target, float maxDelta)
    {
        float delta = target - current;
        if (fabsf(delta) <= maxDelta) return target;
        return current + (delta > 0 ? maxDelta : -maxDelta);
    }

    inline void UpdateColor()
    {
        if (!initialized)
        {
            srand(static_cast<unsigned>(time(nullptr)));
            GenerateNewTargetColor();
            initialized = true;
        }

        if (rainbowModeEnabled)
        {
            bool reachedTarget = true;
            float maxStep = speed * 0.01f;
            for (int i = 0; i < 4; ++i)
            {
                purple[i] = MoveTowards(purple[i], targetColor[i], maxStep);
                customColor[i] = purple[i];
                if (fabsf(purple[i] - targetColor[i]) > 0.01f) reachedTarget = false;
            }
            if (reachedTarget) GenerateNewTargetColor();
        }
        else
        {
            for (int i = 0; i < 4; ++i) purple[i] = customColor[i];
        }
    }

    inline ImVec4 GetPurpleVec4() { return ImVec4(purple[0], purple[1], purple[2], purple[3]); }
    inline ImU32  GetPurpleU32()  { return IM_COL32((int)(purple[0] * 255), (int)(purple[1] * 255), (int)(purple[2] * 255), (int)(purple[3] * 255)); }
    inline void SetRainbowEnabled(bool e) { rainbowModeEnabled = e; }
    inline bool IsRainbowEnabled() { return rainbowModeEnabled; }
    inline void SetSpeed(float s) { speed = fmaxf(0.01f, fminf(s, 5.f)); }
    inline float GetSpeed() { return speed; }
    inline float* GetCustomColorPtr() { return customColor; }
    inline void ResetColor()
    {
        customColor[0] = 0.0f;
        customColor[1] = 0.0f;
        customColor[2] = 0.0f;
        customColor[3] = 1.f;
        for (int i = 0; i < 4; ++i)
            purple[i] = customColor[i];
    }
}
