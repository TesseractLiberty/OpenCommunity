#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/target_icon.h"
#include <algorithm>
#include <cctype>

#ifdef _BACKDOOR
#include "../../game/classes/Player.h"
#include "../../game/classes/Scoreboard.h"
#include "../../game/classes/World.h"
#include "../../../../deps/imgui/imgui.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_set>
#endif

class Target : public Module {
public:
    MODULE_INFO(Target, "Target", "Focuses and filters combat targets using the original loader logic.", ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::target_icon_data, module_icons::target_icon_data_size);

        AddOption(ModuleOption::Text("Player Name", "", 127));
        AddOption(ModuleOption::Toggle("Automatic", false));
        AddOption(ModuleOption::Combo("Mode", { "Low Armor", "Break Armor", "Health", "Both", "Browse All Players" }, 0));
        AddOption(ModuleOption::Combo("Browse Mode", { "Hits", "Time" }, 0));
        AddOption(ModuleOption::SliderInt("Browse Hits", 5, 1, 20));
        AddOption(ModuleOption::SliderInt("Browse Time Ms", 3000, 1, 10000));
        AddOption(ModuleOption::Button("Browse Cache", "Clear Cache"));
        AddOption(ModuleOption::Toggle("Show Browsed Players", false));
        AddOption(ModuleOption::SliderFloat("Both Health Weight", 1.0f, 0.1f, 5.0f));
        AddOption(ModuleOption::SliderFloat("Both Armor Weight", 1.0f, 0.1f, 5.0f));
        AddOption(ModuleOption::Toggle("Consider Durability", true));
        AddOption(ModuleOption::SliderFloat("Break Armor Priority", 5.0f, 0.0f, 10.0f));
    }

    std::string GetTag() const override {
        if (GetAutomatic()) {
#ifdef _BACKDOOR
            if (GetBrowseAllPlayers()) {
                const auto browseInfo = GetBrowseDisplayInfo();
                if (browseInfo.active) {
                    std::string tag = "[A]";
                    if (!browseInfo.clanTag.empty()) {
                        tag += " " + browseInfo.clanTag + "\xC2\xA7r";
                    }
                    if (!browseInfo.currentPlayer.empty()) {
                        tag += " | " + browseInfo.currentPlayer;
                    }
                    tag += " | " + std::to_string(browseInfo.remainingPlayers);
                    return tag;
                }
            }

            const std::string currentTarget = GetCurrentTargetName();
            return currentTarget.empty() ? "[A]" : ("[A] " + currentTarget);
#else
            return "[A]";
#endif
        }

        const std::string targetName = m_Options.size() > kPlayerNameOption ? m_Options[kPlayerNameOption].textValue : std::string{};
        return targetName.empty() ? std::string{} : ("| " + targetName);
    }

    bool ShouldRenderOption(size_t optionIndex) const override {
        if (optionIndex >= m_Options.size()) {
            return false;
        }

        if (optionIndex >= kBaseOptionCount) {
            return GetAutomatic() && GetBrowseAllPlayers() && GetShowBrowsedPlayers();
        }

        switch (optionIndex) {
        case kPlayerNameOption:
            return !GetAutomatic();
        case kModeOption:
            return GetAutomatic();
        case kBrowseModeOption:
            return GetAutomatic() && GetBrowseAllPlayers();
        case kBrowseHitsOption:
            return GetAutomatic() && GetBrowseAllPlayers() && GetBrowseMode() == 0;
        case kBrowseTimeOption:
            return GetAutomatic() && GetBrowseAllPlayers() && GetBrowseMode() == 1;
        case kBrowseClearCacheOption:
        case kShowBrowsedPlayersOption:
            return GetAutomatic() && GetBrowseAllPlayers();
        case kConsiderDurabilityOption:
            return GetAutomatic() && (GetMode() == kModeBreakArmor || GetMode() == kModeBoth);
        case kBrokenArmorPriorityOption:
            return GetAutomatic() && (GetMode() == kModeBreakArmor || GetMode() == kModeBoth);
        case kBothHealthWeightOption:
        case kBothArmorWeightOption:
            return GetAutomatic() && GetMode() == kModeBoth;
        default:
            return true;
        }
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->Target.m_Enabled = IsEnabled();
        config->Modules.m_Target = IsEnabled();

        if (m_Options.size() < kBaseOptionCount) {
            return;
        }

        strncpy_s(config->Target.m_PlayerName, m_Options[kPlayerNameOption].textValue.c_str(), _TRUNCATE);
        config->Target.m_AutoTarget = m_Options[kAutomaticOption].boolValue;
        config->Target.m_TargetSwitch = false;
        const int mode = ClampMode(m_Options[kModeOption].comboIndex);
        config->Target.m_BrowseAllPlayers = mode == kModeBrowseAllPlayers;
        config->Target.m_ShowBrowsedPlayers = m_Options[kShowBrowsedPlayersOption].boolValue;
        config->Target.m_PriorityMode = mode;
        config->Target.m_SwitchMode = ClampBrowseMode(m_Options[kBrowseModeOption].comboIndex);
        config->Target.m_SwitchHits = m_Options[kBrowseHitsOption].intValue;
        config->Target.m_SwitchTimeMs = (std::max)(1, m_Options[kBrowseTimeOption].intValue);
        config->Target.m_BothHealthWeight = m_Options[kBothHealthWeightOption].floatValue;
        config->Target.m_BothArmorWeight = m_Options[kBothArmorWeightOption].floatValue;
        config->Target.m_ConsiderDurability = mode == kModeLowArmor ? true : m_Options[kConsiderDurabilityOption].boolValue;
        config->Target.m_BrokenArmorPriority = m_Options[kBrokenArmorPriorityOption].floatValue;

        if (m_Options[kBrowseClearCacheOption].buttonPressed) {
            config->Target.m_BrowseClearCacheRequested = true;
            m_Options[kBrowseClearCacheOption].buttonPressed = false;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->Target.m_Enabled);
        if (m_Options.size() < kBaseOptionCount) {
            return;
        }

        m_Options[kPlayerNameOption].textValue = config->Target.m_PlayerName;
        m_Options[kAutomaticOption].boolValue = config->Target.m_AutoTarget || config->Target.m_TargetSwitch;
        const int mode = ClampMode(config->Target.m_PriorityMode);
        m_Options[kModeOption].comboIndex = config->Target.m_BrowseAllPlayers ? kModeBrowseAllPlayers : mode;
        m_Options[kBrowseModeOption].comboIndex = ClampBrowseMode(config->Target.m_SwitchMode);
        m_Options[kBrowseHitsOption].intValue = config->Target.m_SwitchHits;
        m_Options[kBrowseTimeOption].intValue = (std::max)(1, config->Target.m_SwitchTimeMs);
        m_Options[kShowBrowsedPlayersOption].boolValue = config->Target.m_ShowBrowsedPlayers;
        m_Options[kBothHealthWeightOption].floatValue = config->Target.m_BothHealthWeight;
        m_Options[kBothArmorWeightOption].floatValue = config->Target.m_BothArmorWeight;
        m_Options[kConsiderDurabilityOption].boolValue = mode == kModeLowArmor ? true : config->Target.m_ConsiderDurability;
        m_Options[kBrokenArmorPriorityOption].floatValue = config->Target.m_BrokenArmorPriority;

        RebuildDynamicOptions(config);
    }

#ifdef _BACKDOOR
    struct BrowseDisplayInfo {
        bool active = false;
        std::string clanTag;
        std::string currentPlayer;
        int remainingPlayers = 0;
    };

    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* env) override;
    void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) override;

    static void OnEntityAttacked(JNIEnv* env, Player* attackedPlayer);
    static void OnLocalAttack(JNIEnv* env, Player* attackedPlayer);
    static std::string GetCurrentTargetName();
    static BrowseDisplayInfo GetBrowseDisplayInfo();
    static void ClearBrowseCache();
    static bool IsTargetActivelyManaging();
#endif

private:
    void RebuildDynamicOptions(const ModuleConfig* config) {
        if (m_Options.size() > kBaseOptionCount) {
            m_Options.resize(kBaseOptionCount);
        }

        if (!config || !GetAutomatic() || !GetBrowseAllPlayers() || !GetShowBrowsedPlayers()) {
            return;
        }

        for (int index = 0; index < config->Target.m_BrowsedPlayersCount && index < 50; ++index) {
            if (!config->Target.m_BrowsedPlayerNames[index][0]) {
                continue;
            }

            ModuleOption option = ModuleOption::ToggleReadOnly(config->Target.m_BrowsedPlayerNames[index], config->Target.m_BrowsedPlayersProcessed[index]);
            option.displayOrder = static_cast<int>(kShowBrowsedPlayersOption * 100 + 1 + index);
            option.showPlayerHead = true;
            option.playerHeadName = config->Target.m_BrowsedPlayerNames[index];
            option.statusOnly = true;
            m_Options.push_back(option);
        }
    }

    bool GetAutomatic() const { return m_Options.size() > kAutomaticOption && m_Options[kAutomaticOption].boolValue; }
    bool GetBrowseAllPlayers() const { return GetMode() == kModeBrowseAllPlayers; }
    bool GetShowBrowsedPlayers() const { return m_Options.size() > kShowBrowsedPlayersOption && m_Options[kShowBrowsedPlayersOption].boolValue; }
    int GetMode() const { return m_Options.size() > kModeOption ? ClampMode(m_Options[kModeOption].comboIndex) : kModeLowArmor; }
    int GetBrowseMode() const { return m_Options.size() > kBrowseModeOption ? ClampBrowseMode(m_Options[kBrowseModeOption].comboIndex) : 0; }

    static int ClampMode(int mode) {
        return mode >= kModeLowArmor && mode <= kModeBrowseAllPlayers ? mode : kModeLowArmor;
    }

    static int ClampBrowseMode(int mode) {
        return mode == 1 ? 1 : 0;
    }

    static constexpr size_t kPlayerNameOption = 0;
    static constexpr size_t kAutomaticOption = 1;
    static constexpr size_t kModeOption = 2;
    static constexpr size_t kBrowseModeOption = 3;
    static constexpr size_t kBrowseHitsOption = 4;
    static constexpr size_t kBrowseTimeOption = 5;
    static constexpr size_t kBrowseClearCacheOption = 6;
    static constexpr size_t kShowBrowsedPlayersOption = 7;
    static constexpr size_t kBothHealthWeightOption = 8;
    static constexpr size_t kBothArmorWeightOption = 9;
    static constexpr size_t kConsiderDurabilityOption = 10;
    static constexpr size_t kBrokenArmorPriorityOption = 11;
    static constexpr size_t kBaseOptionCount = 12;

    static constexpr int kModeLowArmor = 0;
    static constexpr int kModeBreakArmor = 1;
    static constexpr int kModeHealth = 2;
    static constexpr int kModeBoth = 3;
    static constexpr int kModeBrowseAllPlayers = 4;

#ifdef _BACKDOOR
private:
    bool IsValidCombatTarget(JNIEnv* env, Player* player, Player* localPlayer);
    float CalculatePriorityScore(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard);
    float GetBreakArmorVulnerability(JNIEnv* env, Player* player);
    float GetLowArmorScore(JNIEnv* env, Player* player);
    bool IsSameClan(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard);
    void ManageHitboxes(JNIEnv* env, Player* localPlayer, World* world);
    void AutoSelectTarget(JNIEnv* env);
    bool TrySelectBrowseTarget(JNIEnv* env, Player* localPlayer, World* world, Scoreboard* scoreboard, const std::string& previousTarget, std::string& nextTarget);

    std::string GetLockedTarget() const;
    void SetLockedTarget(const std::string& name);
    void ClearLockedTarget();
    bool HasLockedTarget() const;

    mutable std::mutex m_TargetMutex;
    std::string m_LockedTargetName;
    std::chrono::steady_clock::time_point m_LastBrowseSwitchTime{};
    int m_BrowseHitCount = 0;
    bool m_WasEnabled = false;

    static constexpr double kMaxTargetDistance = 6.0;
    static constexpr int kMaxPlayersToProcess = 50;
#endif
};
