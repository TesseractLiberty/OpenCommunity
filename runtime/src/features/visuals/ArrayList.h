#pragma once

#include <algorithm>

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/view_details_icon.h"

class ArrayList : public Module {
public:
    MODULE_INFO(ArrayList, "ArrayList", "Displays active modules on the HUD.", ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::view_details_icon_data, module_icons::view_details_icon_data_size);
        SetEnabled(true);
        AddOption(ModuleOption::Combo("Mode", { "Default", "Rise", "Tesseract", "VapeV4" }, static_cast<int>(ArrayListMode::Default)));
        AddOption(ModuleOption::Toggle("Watermark", true));
        AddOption(ModuleOption::Toggle("Spaced Modules", false));
        AddOption(ModuleOption::Toggle("Wave", true));
        AddOption(ModuleOption::Color("Primary Color", 78.0f / 255.0f, 86.0f / 255.0f, 107.0f / 255.0f, 1.0f));
        AddOption(ModuleOption::Color("Secondary Color", 120.0f / 255.0f, 146.0f / 255.0f, 214.0f / 255.0f, 1.0f));
    }

    bool ShouldRenderOption(size_t optionIndex) const override {
        if (optionIndex == kWaveOption || optionIndex == kPrimaryColorOption || optionIndex == kSecondaryColorOption) {
            const bool defaultMode = m_Options.size() > kModeOption &&
                m_Options[kModeOption].comboIndex == static_cast<int>(ArrayListMode::Default);
            if (!defaultMode) {
                return false;
            }

            if (optionIndex == kSecondaryColorOption) {
                return m_Options.size() > kWaveOption && m_Options[kWaveOption].boolValue;
            }

            return true;
        }

        return true;
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;

        config->HUD.m_Enabled = IsEnabled();
        if (m_Options.size() > kModeOption) {
            config->HUD.m_Mode = m_Options[kModeOption].comboIndex;
        }
        if (m_Options.size() > kWatermarkOption) {
            config->HUD.m_Watermark = m_Options[kWatermarkOption].boolValue;
        }
        if (m_Options.size() > kSpacedModulesOption) {
            config->HUD.m_SpacedModules = m_Options[kSpacedModulesOption].boolValue;
        }
        if (m_Options.size() > kWaveOption) {
            config->HUD.m_Wave = m_Options[kWaveOption].boolValue;
        }
        if (m_Options.size() > kPrimaryColorOption) {
            for (int index = 0; index < 4; ++index) {
                config->HUD.m_PrimaryColor[index] = m_Options[kPrimaryColorOption].colorValue[index];
            }
        }
        if (m_Options.size() > kSecondaryColorOption) {
            for (int index = 0; index < 4; ++index) {
                config->HUD.m_SecondaryColor[index] = m_Options[kSecondaryColorOption].colorValue[index];
            }
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;

        SetEnabled(config->HUD.m_Enabled);
        if (m_Options.size() > kModeOption) {
            const int maxModeIndex = static_cast<int>(m_Options[kModeOption].comboItems.size()) - 1;
            m_Options[kModeOption].comboIndex = (std::clamp)(config->HUD.m_Mode, 0, maxModeIndex);
        }
        if (m_Options.size() > kWatermarkOption) {
            m_Options[kWatermarkOption].boolValue = config->HUD.m_Watermark;
        }
        if (m_Options.size() > kSpacedModulesOption) {
            m_Options[kSpacedModulesOption].boolValue = config->HUD.m_SpacedModules;
        }
        if (m_Options.size() > kWaveOption) {
            m_Options[kWaveOption].boolValue = config->HUD.m_Wave;
        }
        if (m_Options.size() > kPrimaryColorOption) {
            for (int index = 0; index < 4; ++index) {
                m_Options[kPrimaryColorOption].colorValue[index] = config->HUD.m_PrimaryColor[index];
            }
        }
        if (m_Options.size() > kSecondaryColorOption) {
            for (int index = 0; index < 4; ++index) {
                m_Options[kSecondaryColorOption].colorValue[index] = config->HUD.m_SecondaryColor[index];
            }
        }
    }

private:
    static constexpr size_t kModeOption = 0;
    static constexpr size_t kWatermarkOption = 1;
    static constexpr size_t kSpacedModulesOption = 2;
    static constexpr size_t kWaveOption = 3;
    static constexpr size_t kPrimaryColorOption = 4;
    static constexpr size_t kSecondaryColorOption = 5;
};
