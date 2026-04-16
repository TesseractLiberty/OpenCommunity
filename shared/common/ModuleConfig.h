#pragma once

struct ModuleConfig {
    bool m_Destruct = false;
    bool m_FullDestruct = false;
    char m_Username[64] = {};

    struct {
        bool m_Enabled = false;
        bool m_Watermark = true;
        bool m_ColorBar = true;
        bool m_Rainbow = true;
        bool m_Background = true;
        float m_RainbowSpeed = 1.0f;
        float m_Color[4] = { 78.0f / 255.0f, 86.0f / 255.0f, 107.0f / 255.0f, 1.0f };
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
        bool m_Enabled = false;
        int m_Delay = 40;
        int m_Percentage = 50;
    } ArmorFilter;

    struct {
        bool m_Enabled = false;
        int m_Delay = 80;
        bool m_SwapAll = false;
        int m_Percentage = 25;
        int m_HelmetPct = 25;
        int m_ChestPct = 25;
        int m_LegsPct = 25;
        int m_BootsPct = 25;
        bool m_AutoDrop = false;
        bool m_MultiSwap = false;
        bool m_OpenInventory = true;
        bool m_InventoryOrganizer = false;
        int m_HelmetSlot = 1;
        int m_ChestSlot = 2;
        int m_LegsSlot = 3;
        int m_BootsSlot = 4;
    } ArmorSwap;

    struct {
        bool m_Enabled = false;
        int m_DelaySeconds = 5;
    } AutoGapple;

    struct {
        bool m_Enabled = false;
    } NoHitDelay;

    struct {
        bool m_Enabled = false;
    } NoJumpDelay;

    struct {
        bool m_Enabled = false;
        float m_Scale = 1.0f;
        float m_X = 0.5f;
        float m_Y = 0.5f;
    } DamageIndicator;

    struct {
        bool m_AutoClicker = false;
        bool m_ArmorFilter = false;
        bool m_ArmorSwap = false;
        bool m_AutoGapple = false;
        bool m_NoHitDelay = false;
        bool m_NoJumpDelay = false;
        bool m_DamageIndicator = false;
        bool m_RightClicker = false;
        bool m_WTap = false;
        bool m_AimAssist = false;
        bool m_BackTrack = false;
    } Modules;

    void Reset() {
        *this = ModuleConfig{};
    }
};

constexpr size_t CONFIG_SIZE = sizeof(ModuleConfig);
