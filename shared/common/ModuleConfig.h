#pragma once

#include <cstddef>
#include <cstdint>

enum class ArrayListMode {
    Default = 0,
    Rise = 1,
    Tesseract = 2,
    VapeV4 = 3
};

enum class DamageIndicatorMode {
    J3Ultimate = 0,
    Astralis = 1
};

struct ModuleConfig {
    static constexpr std::uint32_t kMagic = 0x4746434F; // OCFG
    static constexpr std::uint32_t kVersion = 8;

    std::uint32_t m_Magic = kMagic;
    std::uint32_t m_Version = kVersion;
    std::uint32_t m_Size = 0;
    std::uint32_t m_Reserved = 0;

    bool m_Destruct = false;
    bool m_FullDestruct = false;
    char m_Username[64] = {};

    struct {
        bool m_Enabled = true;
        bool m_Watermark = true;
        bool m_SpacedModules = false;
        bool m_ColorBar = true;
        bool m_Wave = true;
        bool m_Background = true;
        float m_WaveSpeed = 1.0f;
        float m_PrimaryColor[4] = { 78.0f / 255.0f, 86.0f / 255.0f, 107.0f / 255.0f, 1.0f };
        float m_SecondaryColor[4] = { 120.0f / 255.0f, 146.0f / 255.0f, 214.0f / 255.0f, 1.0f };
        int m_Mode = static_cast<int>(ArrayListMode::Default);
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
        char m_PlayerName[128] = {};
        char m_OnlinePlayerNames[100][17] = {};
        int m_OnlinePlayersCount = 0;
        bool m_AutoTarget = false;
        bool m_TargetSwitch = false;
        bool m_BrowseAllPlayers = false;
        bool m_BrowseClearCacheRequested = false;
        bool m_ShowBrowsedPlayers = false;
        bool m_BrowsedPlayersProcessed[50] = {};
        char m_BrowsedPlayerNames[50][17] = {};
        int m_BrowsedPlayersCount = 0;
        int m_PriorityMode = 0;
        float m_BothHealthWeight = 1.0f;
        float m_BothArmorWeight = 1.0f;
        bool m_ConsiderDurability = true;
        float m_BrokenArmorPriority = 5.0f;
        int m_SwitchMode = 0;
        int m_SwitchHits = 5;
        int m_SwitchTimeMs = 3000;
    } Target;

    struct {
        bool m_Enabled = false;
        bool m_ShowAllies = false;
        int m_ShowAlliesMode = 0;
        int m_ShowAlliesCount = 5;
        bool m_ManualShowList[50] = {};
        char m_ManualNames[50][16] = {};
        int m_ManualCount = 0;
    } HideClans;

    struct {
        bool m_Enabled = false;
    } Nametags;

    struct EnemyInfoListEntry {
        char m_Name[17] = {};
        int m_RemainingDurability[4] = {};
        int m_MaxDurability[4] = {};
        bool m_HasArmor[4] = {};
        bool m_InWeb = false;
        bool m_InRender = false;
    };

    struct {
        bool m_Enabled = false;
        bool m_OpenSecondApplicationRequested = false;
        bool m_FocusSecondApplicationRequested = false;
        bool m_SecondApplicationOpen = false;
        std::uint32_t m_SecondApplicationPid = 0;
        std::uint64_t m_SecondApplicationWindow = 0;
        char m_TrackedClan[32] = {};
        char m_TrackedClanFormatted[64] = {};
        char m_TrackedTargetName[17] = {};
        EnemyInfoListEntry m_Entries[15] = {};
        int m_EntryCount = 0;
    } EnemyInfoList;

    struct {
        bool m_Enabled = true;
    } Notifications;

    struct {
        bool m_Enabled = false;
        bool m_AllEntities = true;
        bool m_Items = true;
        bool m_Players = true;
        bool m_Mobs = true;
        bool m_Animals = true;
        bool m_ArmorStand = true;
        bool m_AutoReset = true;
        float m_MaxRenderRange = 4.0f;
    } NoRender;

    struct {
        bool m_Enabled = false;
        int m_Mode = 0;
        int m_Percentage = 0;
        bool m_NotifyDrops = false;
        bool m_NotifyPickups = false;
    } ItemChams;

    struct {
        bool m_Enabled = false;
    } NoHitDelay;

    struct {
        bool m_Enabled = false;
    } NoJumpDelay;

    struct {
        bool m_Enabled = false;
        int m_Mode = static_cast<int>(DamageIndicatorMode::J3Ultimate);
        float m_Color[4] = { 242.0f / 255.0f, 141.0f / 255.0f, 39.0f / 255.0f, 1.0f };
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
        bool m_Target = false;
        bool m_HideClans = false;
        bool m_Nametags = false;
        bool m_EnemyInfoList = false;
        bool m_Notifications = true;
        bool m_NoRender = false;
        bool m_ItemChams = false;
        bool m_RightClicker = false;
        bool m_WTap = false;
        bool m_AimAssist = false;
        bool m_BackTrack = false;
    } Modules;

    bool IsCompatible() const {
        return m_Magic == kMagic && m_Version == kVersion && m_Size == sizeof(ModuleConfig);
    }

    void StampSchema() {
        m_Magic = kMagic;
        m_Version = kVersion;
        m_Size = static_cast<std::uint32_t>(sizeof(ModuleConfig));
        m_Reserved = 0;
    }

    void Reset() {
        *this = ModuleConfig{};
        StampSchema();
    }

    static ModuleConfig CreateDefault() {
        ModuleConfig config;
        config.StampSchema();
        return config;
    }
};

constexpr size_t CONFIG_SIZE = sizeof(ModuleConfig);
