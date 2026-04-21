#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/armor_filter_icon.h"

#ifdef _RUNTIME
#include "../../game/classes/Container.h"
#include "../../game/classes/GuiScreen.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/PlayerController.h"
#include "../../game/classes/Slot.h"
#include "../../game/jni/GameInstance.h"
#include <chrono>
#endif

class ArmorFilter : public Module {
public:
    MODULE_INFO(ArmorFilter, "ArmorFilter", "Drops low durability armor from your inventory.", ModuleCategory::Combat) {
        SetImagePrefix(module_icons::armor_filter_icon_data, module_icons::armor_filter_icon_data_size);
        AddOption(ModuleOption::SliderInt("Delay", 40, 10, 500));
        AddOption(ModuleOption::SliderInt("Durability", 50, 0, 100));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->ArmorFilter.m_Enabled = IsEnabled();
        config->Modules.m_ArmorFilter = IsEnabled();
        if (m_Options.size() >= 2) {
            config->ArmorFilter.m_Delay = m_Options[0].intValue;
            config->ArmorFilter.m_Percentage = m_Options[1].intValue;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->ArmorFilter.m_Enabled);
        if (m_Options.size() >= 2) {
            m_Options[0].intValue = config->ArmorFilter.m_Delay;
            m_Options[1].intValue = config->ArmorFilter.m_Percentage;
        }
    }

    std::string GetTag() const override {
        const int delay = m_Options.size() >= 1 ? m_Options[0].intValue : 0;
        const int percentage = m_Options.size() >= 2 ? m_Options[1].intValue : 0;

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%dms | %d%%", delay, percentage);
        return buffer;
    }

#ifdef _RUNTIME
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void Run(JNIEnv* env);

    int GetDelay() const { return m_Options[0].intValue; }
    int GetDurabilityThreshold() const { return m_Options[1].intValue; }

private:
    std::chrono::steady_clock::time_point m_LastActionTime = std::chrono::steady_clock::now();
#endif
};
