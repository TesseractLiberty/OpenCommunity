#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/no_hit_delay_icon.h"

#ifdef _BACKDOOR
#include "../../game/classes/Minecraft.h"
#endif

class NoHitDelay : public Module {
public:
    MODULE_INFO(NoHitDelay, "NoHitDelay", "Removes the attack delay after hits.", ModuleCategory::Combat) {
        SetImagePrefix(module_icons::no_hit_delay_icon_data, module_icons::no_hit_delay_icon_data_size);
    }

    bool SupportsKeybind() const override { return false; }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->NoHitDelay.m_Enabled = IsEnabled();
        config->Modules.m_NoHitDelay = IsEnabled();
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->NoHitDelay.m_Enabled);
    }

#ifdef _BACKDOOR
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void Run(JNIEnv* env);
#endif
};
