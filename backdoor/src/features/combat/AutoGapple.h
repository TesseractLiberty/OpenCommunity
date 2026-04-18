#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/apple_icon.h"

#ifdef _BACKDOOR
#include "../../game/classes/InventoryPlayer.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/KeyBinding.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/PlayerController.h"
#include "../../game/classes/PotionEffect.h"
#endif

class AutoGapple : public Module {
public:
    MODULE_INFO(AutoGapple, "AutoGapple", "Automatically eats a golden apple when regeneration is low.", ModuleCategory::Combat) {
        SetImagePrefix(module_icons::apple_icon_data, module_icons::apple_icon_data_size);
        AddOption(ModuleOption::SliderInt("Delay", 5, 1, 28));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->AutoGapple.m_Enabled = IsEnabled();
        config->Modules.m_AutoGapple = IsEnabled();
        if (!m_Options.empty()) {
            config->AutoGapple.m_DelaySeconds = m_Options[0].intValue;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->AutoGapple.m_Enabled);
        if (!m_Options.empty()) {
            m_Options[0].intValue = config->AutoGapple.m_DelaySeconds;
        }
    }

    std::string GetTag() const override {
        const int delaySeconds = !m_Options.empty() ? m_Options[0].intValue : 0;
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%ds", delaySeconds);
        return buffer;
    }

#ifdef _BACKDOOR
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void Run(JNIEnv* env);

private:
    int GetDelaySeconds() const { return !m_Options.empty() ? m_Options[0].intValue : 5; }
#endif
};
