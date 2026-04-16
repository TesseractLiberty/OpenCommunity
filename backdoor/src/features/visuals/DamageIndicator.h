#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/heart_icon.h"

#ifdef _BACKDOOR
#include "../../game/classes/GuiScreen.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/MovingObjectPosition.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/RenderHelper.h"
#include "../../game/classes/RenderItem.h"
#include "../../../../deps/imgui/fonts.hpp"
#include "../../../../deps/imgui/imgui.h"
#endif

class DamageIndicator : public Module {
public:
    MODULE_INFO(DamageIndicator, "DamageIndicator", "Displays information about the player you're aiming at.", ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::heart_icon_data, module_icons::heart_icon_data_size);
        AddOption(ModuleOption::SliderFloat("Scale", 1.0f, 0.5f, 2.0f));
        AddOption(ModuleOption::SliderFloat("X", 0.5f, 0.0f, 1.0f));
        AddOption(ModuleOption::SliderFloat("Y", 0.5f, 0.0f, 1.0f));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->DamageIndicator.m_Enabled = IsEnabled();
        config->Modules.m_DamageIndicator = IsEnabled();
        if (m_Options.size() >= 3) {
            config->DamageIndicator.m_Scale = m_Options[0].floatValue;
            config->DamageIndicator.m_X = m_Options[1].floatValue;
            config->DamageIndicator.m_Y = m_Options[2].floatValue;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->DamageIndicator.m_Enabled);
        if (m_Options.size() >= 3) {
            m_Options[0].floatValue = config->DamageIndicator.m_Scale;
            m_Options[1].floatValue = config->DamageIndicator.m_X;
            m_Options[2].floatValue = config->DamageIndicator.m_Y;
        }
    }

#ifdef _BACKDOOR
    void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) override;

private:
    float GetScale() const { return m_Options.size() >= 1 ? m_Options[0].floatValue : 1.0f; }
    float GetAnchorX() const { return m_Options.size() >= 2 ? m_Options[1].floatValue : 0.5f; }
    float GetAnchorY() const { return m_Options.size() >= 3 ? m_Options[2].floatValue : 0.5f; }
#endif
};
