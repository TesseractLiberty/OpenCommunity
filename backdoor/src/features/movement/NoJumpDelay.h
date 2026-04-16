#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/no_jump_delay_icon.h"

#ifdef _BACKDOOR
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/Player.h"
#endif

class NoJumpDelay : public Module {
public:
    MODULE_INFO(NoJumpDelay, "NoJumpDelay", "Removes the jump cooldown.", ModuleCategory::Movement) {
        SetImagePrefix(module_icons::no_jump_delay_icon_data, module_icons::no_jump_delay_icon_data_size);
    }

    bool SupportsKeybind() const override { return false; }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->NoJumpDelay.m_Enabled = IsEnabled();
        config->Modules.m_NoJumpDelay = IsEnabled();
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->NoJumpDelay.m_Enabled);
    }

#ifdef _BACKDOOR
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void Run(JNIEnv* env);
#endif
};
