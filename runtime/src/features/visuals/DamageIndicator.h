#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/heart_icon.h"

#ifdef _RUNTIME
#include "../../game/classes/GuiScreen.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/MovingObjectPosition.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/RenderHelper.h"
#include "../../game/classes/RenderItem.h"
#include "../../../../deps/imgui/fonts/fonts.hpp"
#include "../../../../deps/imgui/imgui.h"
#endif

class DamageIndicator : public Module {
public:
    MODULE_INFO(DamageIndicator, XOR("DamageIndicator"), XOR("Displays information about the player you're aiming at."), ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::heart_icon_data, module_icons::heart_icon_data_size);
        AddOption(ModuleOption::Combo(XOR("Mode"), { XOR("J3 Ultimate"), XOR("Astralis") }, static_cast<int>(DamageIndicatorMode::J3Ultimate)));
        AddOption(ModuleOption::Color(XOR("Color"), 242.0f / 255.0f, 141.0f / 255.0f, 39.0f / 255.0f, 1.0f));
        AddOption(ModuleOption::SliderFloat(XOR("Scale"), 1.0f, 0.5f, 2.0f));
        AddOption(ModuleOption::SliderFloat(XOR("X"), 0.5f, 0.0f, 1.0f));
        AddOption(ModuleOption::SliderFloat(XOR("Y"), 0.5f, 0.0f, 1.0f));
    }

    bool ShouldRenderOption(size_t optionIndex) const override {
        return optionIndex != kColorOption || GetMode() == DamageIndicatorMode::Astralis;
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->DamageIndicator.m_Enabled = IsEnabled();
        config->Modules.m_DamageIndicator = IsEnabled();
        if (m_Options.size() >= kOptionCount) {
            config->DamageIndicator.m_Mode = m_Options[kModeOption].comboIndex;
            for (int i = 0; i < 4; ++i) {
                config->DamageIndicator.m_Color[i] = m_Options[kColorOption].colorValue[i];
            }
            config->DamageIndicator.m_Scale = m_Options[kScaleOption].floatValue;
            config->DamageIndicator.m_X = m_Options[kXOption].floatValue;
            config->DamageIndicator.m_Y = m_Options[kYOption].floatValue;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->DamageIndicator.m_Enabled);
        if (m_Options.size() >= kOptionCount) {
            m_Options[kModeOption].comboIndex = config->DamageIndicator.m_Mode;
            for (int i = 0; i < 4; ++i) {
                m_Options[kColorOption].colorValue[i] = config->DamageIndicator.m_Color[i];
            }
            m_Options[kScaleOption].floatValue = config->DamageIndicator.m_Scale;
            m_Options[kXOption].floatValue = config->DamageIndicator.m_X;
            m_Options[kYOption].floatValue = config->DamageIndicator.m_Y;
        }
    }

    std::string GetTag() const override {
        const int idx = static_cast<int>(GetMode());
        if (m_Options.size() > kModeOption && idx >= 0 && idx < static_cast<int>(m_Options[kModeOption].comboItems.size())) {
            return std::string(m_Options[kModeOption].comboItems[static_cast<size_t>(idx)].c_str());
        }
        return {};
    }

private:
    static constexpr size_t kModeOption = 0;
    static constexpr size_t kColorOption = 1;
    static constexpr size_t kScaleOption = 2;
    static constexpr size_t kXOption = 3;
    static constexpr size_t kYOption = 4;
    static constexpr size_t kOptionCount = 5;

    DamageIndicatorMode GetMode() const {
        if (m_Options.size() <= kModeOption) {
            return DamageIndicatorMode::J3Ultimate;
        }
        const int modeIndex = (std::clamp)(m_Options[kModeOption].comboIndex, 0, static_cast<int>(DamageIndicatorMode::Astralis));
        return static_cast<DamageIndicatorMode>(modeIndex);
    }

    float GetScale() const { return m_Options.size() > kScaleOption ? m_Options[kScaleOption].floatValue : 1.0f; }
    float GetAnchorX() const { return m_Options.size() > kXOption ? m_Options[kXOption].floatValue : 0.5f; }
    float GetAnchorY() const { return m_Options.size() > kYOption ? m_Options[kYOption].floatValue : 0.5f; }
    const float* GetAccentColor() const {
        static constexpr float kDefaultColor[4] = { 242.0f / 255.0f, 141.0f / 255.0f, 39.0f / 255.0f, 1.0f };
        return m_Options.size() > kColorOption ? m_Options[kColorOption].colorValue : kDefaultColor;
    }

#ifdef _RUNTIME
public:
    static void SetFonts(ImFont* regular, ImFont* bold);
    static void ReleaseRenderResources();

    void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) override;

private:
    std::string m_LastTargetName;
    float m_AnimatedHealth = -1.0f;
    std::chrono::steady_clock::time_point m_LastAnimationTime = std::chrono::steady_clock::now();

    static ImFont* s_OpenSansRegularFont;
    static ImFont* s_OpenSansBoldFont;
#endif
};
