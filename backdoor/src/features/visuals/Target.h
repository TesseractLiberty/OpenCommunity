#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/target_icon.h"
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
        AddOption(ModuleOption::Toggle("Auto Target", false));
        AddOption(ModuleOption::Toggle("Target Switch", false));
        AddOption(ModuleOption::Toggle("Browse All Players", false));
        AddOption(ModuleOption::Button("Browse Cache", "Clear Cache"));
        AddOption(ModuleOption::Toggle("Show Browsed Players", false));
        AddOption(ModuleOption::Combo("Priority Mode", { "Health", "Armor", "Both" }, 0));
        AddOption(ModuleOption::SliderFloat("Both Health Weight", 1.0f, 0.1f, 5.0f));
        AddOption(ModuleOption::SliderFloat("Both Armor Weight", 1.0f, 0.1f, 5.0f));
        AddOption(ModuleOption::Toggle("Consider Durability", true));
        AddOption(ModuleOption::SliderFloat("Broken Armor Priority", 5.0f, 0.0f, 10.0f));
        AddOption(ModuleOption::Combo("Switch Mode", { "Hits", "Time" }, 0));
        AddOption(ModuleOption::SliderInt("Switch Hits", 5, 1, 20));
        AddOption(ModuleOption::SliderInt("Switch Time Ms", 3000, 250, 10000));
    }

    std::string GetTag() const override {
        if (GetAutoTarget()) {
#ifdef _BACKDOOR
            const std::string currentTarget = GetCurrentTargetName();
            return currentTarget.empty() ? "[A]" : ("[A] " + currentTarget);
#else
            return "[A]";
#endif
        }

        if (GetTargetSwitch()) {
#ifdef _BACKDOOR
            if (GetBrowseAllPlayers()) {
                const auto browseInfo = GetBrowseDisplayInfo();
                if (browseInfo.active) {
                    std::string tag = "[S]";
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
            return currentTarget.empty() ? "[S]" : ("[S] " + currentTarget);
#else
            return "[S]";
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
            return GetTargetSwitch() && GetBrowseAllPlayers() && GetShowBrowsedPlayers();
        }

        switch (optionIndex) {
        case kPlayerNameOption:
            return !GetAutoTarget() && !GetTargetSwitch();
        case kBrowseAllPlayersOption:
            return GetTargetSwitch();
        case kBrowseClearCacheOption:
        case kShowBrowsedPlayersOption:
            return GetTargetSwitch() && GetBrowseAllPlayers();
        case kPriorityModeOption:
        case kConsiderDurabilityOption:
        case kBrokenArmorPriorityOption:
            return GetAutoTarget();
        case kBothHealthWeightOption:
        case kBothArmorWeightOption:
            return GetAutoTarget() && GetPriorityMode() == 2;
        case kSwitchModeOption:
            return GetTargetSwitch();
        case kSwitchHitsOption:
            return GetTargetSwitch() && GetSwitchMode() == 0;
        case kSwitchTimeOption:
            return GetTargetSwitch() && GetSwitchMode() == 1;
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
        config->Target.m_AutoTarget = m_Options[kAutoTargetOption].boolValue;
        config->Target.m_TargetSwitch = m_Options[kTargetSwitchOption].boolValue;
        config->Target.m_BrowseAllPlayers = m_Options[kBrowseAllPlayersOption].boolValue;
        config->Target.m_ShowBrowsedPlayers = m_Options[kShowBrowsedPlayersOption].boolValue;
        config->Target.m_PriorityMode = m_Options[kPriorityModeOption].comboIndex;
        config->Target.m_BothHealthWeight = m_Options[kBothHealthWeightOption].floatValue;
        config->Target.m_BothArmorWeight = m_Options[kBothArmorWeightOption].floatValue;
        config->Target.m_ConsiderDurability = m_Options[kConsiderDurabilityOption].boolValue;
        config->Target.m_BrokenArmorPriority = m_Options[kBrokenArmorPriorityOption].floatValue;
        config->Target.m_SwitchMode = m_Options[kSwitchModeOption].comboIndex;
        config->Target.m_SwitchHits = m_Options[kSwitchHitsOption].intValue;
        config->Target.m_SwitchTimeMs = m_Options[kSwitchTimeOption].intValue;

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
        m_Options[kAutoTargetOption].boolValue = config->Target.m_AutoTarget;
        m_Options[kTargetSwitchOption].boolValue = config->Target.m_TargetSwitch;
        m_Options[kBrowseAllPlayersOption].boolValue = config->Target.m_BrowseAllPlayers;
        m_Options[kShowBrowsedPlayersOption].boolValue = config->Target.m_ShowBrowsedPlayers;
        m_Options[kPriorityModeOption].comboIndex = config->Target.m_PriorityMode;
        m_Options[kBothHealthWeightOption].floatValue = config->Target.m_BothHealthWeight;
        m_Options[kBothArmorWeightOption].floatValue = config->Target.m_BothArmorWeight;
        m_Options[kConsiderDurabilityOption].boolValue = config->Target.m_ConsiderDurability;
        m_Options[kBrokenArmorPriorityOption].floatValue = config->Target.m_BrokenArmorPriority;
        m_Options[kSwitchModeOption].comboIndex = config->Target.m_SwitchMode;
        m_Options[kSwitchHitsOption].intValue = config->Target.m_SwitchHits;
        m_Options[kSwitchTimeOption].intValue = config->Target.m_SwitchTimeMs;

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

        if (!config || !GetTargetSwitch() || !GetBrowseAllPlayers() || !GetShowBrowsedPlayers()) {
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

    bool GetAutoTarget() const { return m_Options.size() > kAutoTargetOption && m_Options[kAutoTargetOption].boolValue; }
    bool GetTargetSwitch() const { return m_Options.size() > kTargetSwitchOption && m_Options[kTargetSwitchOption].boolValue; }
    bool GetBrowseAllPlayers() const { return m_Options.size() > kBrowseAllPlayersOption && m_Options[kBrowseAllPlayersOption].boolValue; }
    bool GetShowBrowsedPlayers() const { return m_Options.size() > kShowBrowsedPlayersOption && m_Options[kShowBrowsedPlayersOption].boolValue; }
    int GetPriorityMode() const { return m_Options.size() > kPriorityModeOption ? m_Options[kPriorityModeOption].comboIndex : 0; }
    int GetSwitchMode() const { return m_Options.size() > kSwitchModeOption ? m_Options[kSwitchModeOption].comboIndex : 0; }

    static constexpr size_t kPlayerNameOption = 0;
    static constexpr size_t kAutoTargetOption = 1;
    static constexpr size_t kTargetSwitchOption = 2;
    static constexpr size_t kBrowseAllPlayersOption = 3;
    static constexpr size_t kBrowseClearCacheOption = 4;
    static constexpr size_t kShowBrowsedPlayersOption = 5;
    static constexpr size_t kPriorityModeOption = 6;
    static constexpr size_t kBothHealthWeightOption = 7;
    static constexpr size_t kBothArmorWeightOption = 8;
    static constexpr size_t kConsiderDurabilityOption = 9;
    static constexpr size_t kBrokenArmorPriorityOption = 10;
    static constexpr size_t kSwitchModeOption = 11;
    static constexpr size_t kSwitchHitsOption = 12;
    static constexpr size_t kSwitchTimeOption = 13;
    static constexpr size_t kBaseOptionCount = 14;

#ifdef _BACKDOOR
private:
    bool IsCurrentTargetInvalid(JNIEnv* env, Player* localPlayer);
    bool IsValidCombatTarget(JNIEnv* env, Player* player, Player* localPlayer);
    float CalculatePriorityScore(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard);
    float GetArmorVulnerability(JNIEnv* env, Player* player);
    bool IsSameClan(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard);
    void ManageHitboxes(JNIEnv* env, Player* localPlayer, World* world);
    void AutoSelectTarget(JNIEnv* env);
    void SwitchToNextTarget(JNIEnv* env);
    bool TrySelectBrowseTarget(JNIEnv* env, Player* localPlayer, World* world, Scoreboard* scoreboard, const std::string& previousTarget, std::string& nextTarget);

    std::string GetLockedTarget() const;
    void SetLockedTarget(const std::string& name);
    void ClearLockedTarget();
    bool HasLockedTarget() const;

    mutable std::mutex m_TargetMutex;
    std::string m_LockedTargetName;
    std::chrono::steady_clock::time_point m_LastSwitchTime{};
    std::chrono::steady_clock::time_point m_TargetLockedTime{};
    int m_HitCount = 0;
    int m_PreviousSwingProgressInt = 0;
    bool m_PreviousPhysicalClick = false;
    bool m_WasEnabled = false;

    static constexpr double kMaxTargetDistance = 6.0;
    static constexpr int kMaxPlayersToProcess = 50;
    static constexpr int kSwitchCooldownMs = 500;
#endif
};
