#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/view_details_icon.h"

// @ModuleInfo(name = "ArrayList", description = "Displays active modules on the HUD.",
//        category = ModuleCategory.VISUALS)
// imagePrefix = module_icons::view_details_icon_data

class ArrayList : public Module {
public:
    MODULE_INFO(ArrayList, "ArrayList", "Displays active modules on the HUD.", ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::view_details_icon_data, module_icons::view_details_icon_data_size);
        AddOption(ModuleOption::Toggle("Watermark", true));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;
        config->HUD.m_Enabled = IsEnabled();
        if (!m_Options.empty()) {
            config->HUD.m_Watermark = m_Options[0].boolValue;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;
        SetEnabled(config->HUD.m_Enabled);
        if (!m_Options.empty()) {
            m_Options[0].boolValue = config->HUD.m_Watermark;
        }
    }
};
