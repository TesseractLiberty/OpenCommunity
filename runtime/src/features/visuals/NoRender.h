#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/no_render_icon.h"

#include <algorithm>

#ifdef _RUNTIME
#include <jni.h>
#endif

class NoRender : public Module {
public:
    MODULE_INFO(NoRender, "NoRender", "Controls selected entity rendering.", ModuleCategory::Visuals) {
        SetBeta();
        SetImagePrefix(module_icons::no_render_icon_data, module_icons::no_render_icon_data_size);
        AddOption(ModuleOption::Toggle("AllEntities", true));
        AddOption(ModuleOption::Toggle("Items", true));
        AddOption(ModuleOption::Toggle("Players", true));
        AddOption(ModuleOption::Toggle("Mobs", true));
        AddOption(ModuleOption::Toggle("Animals", true));
        AddOption(ModuleOption::Toggle("ArmorStand", true));
        AddOption(ModuleOption::Toggle("AutoReset", true));
        AddOption(ModuleOption::SliderFloat("MaxRenderRange", 4.0f, 0.0f, 16.0f));
    }

    bool ShouldRenderOption(size_t optionIndex) const override {
        if (optionIndex == kItemsOption || optionIndex == kAnimalsOption || optionIndex == kArmorStandOption) {
            return !GetBoolOption(kAllEntitiesOption, true);
        }

        return true;
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->NoRender.m_Enabled = IsEnabled();
        config->NoRender.m_AllEntities = GetBoolOption(kAllEntitiesOption, true);
        config->NoRender.m_Items = GetBoolOption(kItemsOption, true);
        config->NoRender.m_Players = GetBoolOption(kPlayersOption, true);
        config->NoRender.m_Mobs = GetBoolOption(kMobsOption, true);
        config->NoRender.m_Animals = GetBoolOption(kAnimalsOption, true);
        config->NoRender.m_ArmorStand = GetBoolOption(kArmorStandOption, true);
        config->NoRender.m_AutoReset = GetBoolOption(kAutoResetOption, true);
        config->NoRender.m_MaxRenderRange = GetFloatOption(kMaxRenderRangeOption, 4.0f);
        config->Modules.m_NoRender = IsEnabled();
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->NoRender.m_Enabled);
        SetBoolOption(kAllEntitiesOption, config->NoRender.m_AllEntities);
        SetBoolOption(kItemsOption, config->NoRender.m_Items);
        SetBoolOption(kPlayersOption, config->NoRender.m_Players);
        SetBoolOption(kMobsOption, config->NoRender.m_Mobs);
        SetBoolOption(kAnimalsOption, config->NoRender.m_Animals);
        SetBoolOption(kArmorStandOption, config->NoRender.m_ArmorStand);
        SetBoolOption(kAutoResetOption, config->NoRender.m_AutoReset);
        SetFloatOption(kMaxRenderRangeOption, config->NoRender.m_MaxRenderRange, 0.0f, 16.0f);
    }

#ifdef _RUNTIME
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void ShutdownRuntime(void* envPtr) override;

private:
    bool ShouldStopRender(JNIEnv* env, jobject entity, jobject localPlayer) const;
    void ApplyEntityRendering(JNIEnv* env, jobject worldObject, jobject localPlayer);
    void ResetEntityRendering(JNIEnv* env, jobject worldObject, jobject localPlayer);
    void ReleaseWorldRef(JNIEnv* env);

    bool m_WasEnabled = false;
    bool m_AppliedEntityOverrides = false;
    jobject m_LastWorld = nullptr;
#endif

private:
    static constexpr size_t kAllEntitiesOption = 0;
    static constexpr size_t kItemsOption = 1;
    static constexpr size_t kPlayersOption = 2;
    static constexpr size_t kMobsOption = 3;
    static constexpr size_t kAnimalsOption = 4;
    static constexpr size_t kArmorStandOption = 5;
    static constexpr size_t kAutoResetOption = 6;
    static constexpr size_t kMaxRenderRangeOption = 7;

    bool GetBoolOption(size_t optionIndex, bool fallback) const {
        return m_Options.size() > optionIndex ? m_Options[optionIndex].boolValue : fallback;
    }

    float GetFloatOption(size_t optionIndex, float fallback) const {
        return m_Options.size() > optionIndex ? m_Options[optionIndex].floatValue : fallback;
    }

    void SetBoolOption(size_t optionIndex, bool value) {
        if (m_Options.size() > optionIndex) {
            m_Options[optionIndex].boolValue = value;
        }
    }

    void SetFloatOption(size_t optionIndex, float value, float minValue, float maxValue) {
        if (m_Options.size() > optionIndex) {
            m_Options[optionIndex].floatValue = (std::clamp)(value, minValue, maxValue);
        }
    }
};
