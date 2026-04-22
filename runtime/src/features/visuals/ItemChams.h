#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/item_chams_icon.h"

#include <algorithm>

#ifdef _RUNTIME
#include "../../../../deps/imgui/imgui.h"

#include <jni.h>
#include <mutex>
#include <unordered_set>
#endif

class ItemChams : public Module {
public:
    MODULE_INFO(ItemChams, "Item Chams", "Highlights eligible armor items through walls.", ModuleCategory::Visuals) {
        SetBeta();
        SetImagePrefix(module_icons::item_chams_icon_data, module_icons::item_chams_icon_data_size);
        AddOption(ModuleOption::SliderInt("Porcentagem", 0, 0, 100));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->ItemChams.m_Enabled = IsEnabled();
        config->ItemChams.m_Percentage = GetPercentageThreshold();
        config->Modules.m_ItemChams = IsEnabled();
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->ItemChams.m_Enabled);
        SetPercentageThreshold(config->ItemChams.m_Percentage);
    }

#ifdef _RUNTIME
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) override;
    void ShutdownRuntime(void* envPtr) override;

private:
    void ApplyArmorFiltering(JNIEnv* env, jobject worldObject);
    void RestoreHiddenArmorItems(JNIEnv* env, jobject worldObject);
    void ReleaseWorldRef(JNIEnv* env);
    bool WasHiddenByModule(int identityHash) const;
    void RememberHiddenEntity(int identityHash);
    bool ForgetHiddenEntity(int identityHash);
    void ClearHiddenEntities();

    mutable std::mutex m_HiddenMutex;
    std::unordered_set<int> m_HiddenEntityIds;
    bool m_WasEnabled = false;
    jobject m_LastWorld = nullptr;
#endif

private:
    static constexpr size_t kPercentageOption = 0;

    int GetPercentageThreshold() const {
        return m_Options.size() > kPercentageOption ? (std::clamp)(m_Options[kPercentageOption].intValue, 0, 100) : 0;
    }

    void SetPercentageThreshold(int value) {
        if (m_Options.size() > kPercentageOption) {
            m_Options[kPercentageOption].intValue = (std::clamp)(value, 0, 100);
        }
    }
};
