#pragma once

struct ModuleConfig {
    bool m_Destruct = false;
    bool m_FullDestruct = false;
    char m_Username[64] = {};

    struct {
        bool m_Enabled = true;
        bool m_ColorBar = true;
        bool m_Rainbow = false;
        bool m_Background = true;
        float m_RainbowSpeed = 1.0f;
        float m_Color[4] = { 0x92 / 255.0f, 0x39 / 255.0f, 0x4B / 255.0f, 1.0f };
        int m_Style = 1;
    } HUD;

    struct {
        bool m_Enabled = false;
        int m_MinCps = 10;
        int m_MaxCps = 14;
        bool m_Jitter = false;
        bool m_OnlyWhileHolding = true;
    } AutoClicker;

    struct {
        bool m_AutoClicker = false;
        bool m_RightClicker = false;
        bool m_WTap = false;
        bool m_AimAssist = false;
        bool m_BackTrack = false;
    } Modules;

    void Reset() {
        m_Destruct = false;
        m_FullDestruct = false;
    }
};

constexpr size_t CONFIG_SIZE = sizeof(ModuleConfig);
