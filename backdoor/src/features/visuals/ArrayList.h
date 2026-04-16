#pragma once

#include <algorithm>

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/view_details_icon.h"

class ArrayList : public Module {
public:
    MODULE_INFO(ArrayList, "ArrayList", "Displays active modules on the HUD.", ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::view_details_icon_data, module_icons::view_details_icon_data_size);
        SetEnabled(true);
        AddOption(ModuleOption::Combo("Mode", { "Default", "Rise", "Tesseract", "VapeV4" }, static_cast<int>(ArrayListMode::Default)));
        AddOption(ModuleOption::Toggle("Watermark", true));
        AddOption(ModuleOption::Toggle("Spaced Modules", false));
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
    }

private:
    static constexpr size_t kModeOption = 0;
    static constexpr size_t kWatermarkOption = 1;
    static constexpr size_t kSpacedModulesOption = 2;
};
