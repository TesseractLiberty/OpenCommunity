#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"

class HideClans : public Module {
public:
    MODULE_INFO(HideClans, "HideClans", "Hides allied players using the original team and clan rules.", ModuleCategory::Visuals) {
        SetImagePath("Hide-256.png");

        AddOption(ModuleOption::Toggle("Show Allies", false));
        AddOption(ModuleOption::Combo("Show Allies Mode", { "Nearest", "Semi Auto", "Manual" }, 0));
        AddOption(ModuleOption::SliderInt("Show Allies Count", 5, 1, 14));
    }

    std::string GetTag() const override {
        std::string mode;
        if (!GetShowAllies()) {
            mode = "Blatant";
        } else {
            switch (GetShowAlliesMode()) {
            case 0:
                mode = "Automatic";
                break;
            case 1:
                mode = "Semi Automatic";
                break;
            default:
                mode = "Manual";
                break;
            }
        }

#ifdef _BACKDOOR
        const std::string clanTag = GetCachedClanTag();
        if (!clanTag.empty()) {
            mode += " " + clanTag;
        }
#endif

        return mode;
    }

    bool ShouldRenderOption(size_t optionIndex) const override {
        if (optionIndex >= m_Options.size()) {
            return false;
        }

        if (optionIndex >= kBaseOptionCount) {
            return GetShowAllies() && GetShowAlliesMode() == 2;
        }

        switch (optionIndex) {
        case kShowAlliesModeOption:
            return GetShowAllies();
        case kShowAlliesCountOption:
            return GetShowAllies() && GetShowAlliesMode() != 2;
        default:
            return true;
        }
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->HideClans.m_Enabled = IsEnabled();
        config->Modules.m_HideClans = IsEnabled();

        if (m_Options.size() < kBaseOptionCount) {
            return;
        }

        config->HideClans.m_ShowAllies = m_Options[kShowAlliesOption].boolValue;
        config->HideClans.m_ShowAlliesMode = m_Options[kShowAlliesModeOption].comboIndex;
        config->HideClans.m_ShowAlliesCount = m_Options[kShowAlliesCountOption].intValue;

        if (GetShowAllies() && GetShowAlliesMode() == 2) {
            int count = 0;
            for (size_t index = kBaseOptionCount; index < m_Options.size() && count < 50; ++index) {
                strncpy_s(config->HideClans.m_ManualNames[count], m_Options[index].name.c_str(), _TRUNCATE);
                config->HideClans.m_ManualShowList[count] = m_Options[index].boolValue;
                ++count;
            }
            config->HideClans.m_ManualCount = count;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->HideClans.m_Enabled);
        if (m_Options.size() < kBaseOptionCount) {
            return;
        }

        m_Options[kShowAlliesOption].boolValue = config->HideClans.m_ShowAllies;
        m_Options[kShowAlliesModeOption].comboIndex = config->HideClans.m_ShowAlliesMode;
        m_Options[kShowAlliesCountOption].intValue = config->HideClans.m_ShowAlliesCount;

        RebuildDynamicOptions(config);
    }

#ifdef _BACKDOOR
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* env) override;
    static std::string GetCachedClanTag();
#endif

private:
    void RebuildDynamicOptions(const ModuleConfig* config) {
        if (m_Options.size() > kBaseOptionCount) {
            m_Options.resize(kBaseOptionCount);
        }

        if (!config || !GetShowAllies() || GetShowAlliesMode() != 2) {
            return;
        }

        for (int index = 0; index < config->HideClans.m_ManualCount && index < 50; ++index) {
            if (!config->HideClans.m_ManualNames[index][0]) {
                continue;
            }

            ModuleOption option = ModuleOption::Toggle(config->HideClans.m_ManualNames[index], config->HideClans.m_ManualShowList[index]);
            option.displayOrder = static_cast<int>(kShowAlliesModeOption * 100 + 1 + index);
            option.showPlayerHead = true;
            option.playerHeadName = config->HideClans.m_ManualNames[index];
            m_Options.push_back(option);
        }
    }

    bool GetShowAllies() const { return m_Options.size() > kShowAlliesOption && m_Options[kShowAlliesOption].boolValue; }
    int GetShowAlliesMode() const { return m_Options.size() > kShowAlliesModeOption ? m_Options[kShowAlliesModeOption].comboIndex : 0; }

    static constexpr size_t kShowAlliesOption = 0;
    static constexpr size_t kShowAlliesModeOption = 1;
    static constexpr size_t kShowAlliesCountOption = 2;
    static constexpr size_t kBaseOptionCount = 3;
};
