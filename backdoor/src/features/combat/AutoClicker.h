#pragma once

#include "../../../../shared/common/FeatureManager.h"
#include "../../../../deps/imgui/mouse_icon.h"
#include "../../../../shared/common/ModuleConfig.h"
#ifdef _BACKDOOR
#include "../../game/jni/GameInstance.h"
#include "../../game/mapping/Mapper.h"
#include "../../game/jni/Class.h"
#include "../../game/jni/Field.h"
#include "../../game/jni/Method.h"
#include <chrono>
#include <random>
#endif

class AutoClicker : public Module {
public:
    MODULE_INFO(AutoClicker, "AutoClicker", "Automatically clicks for you.", ModuleCategory::Combat) {
        SetImagePrefix(module_icons::mouse_icon_data, module_icons::mouse_icon_data_size);
        AddOption(ModuleOption::SliderInt("Min CPS", 10, 1, 20));
        AddOption(ModuleOption::SliderInt("Max CPS", 14, 1, 20));
        AddOption(ModuleOption::Toggle("Jitter", false));
        AddOption(ModuleOption::Toggle("Only While Holding", true));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;
        config->AutoClicker.m_Enabled = IsEnabled();
        if (m_Options.size() >= 4) {
            config->AutoClicker.m_MinCps = m_Options[0].intValue;
            config->AutoClicker.m_MaxCps = m_Options[1].intValue;
            config->AutoClicker.m_Jitter = m_Options[2].boolValue;
            config->AutoClicker.m_OnlyWhileHolding = m_Options[3].boolValue;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;
        SetEnabled(config->AutoClicker.m_Enabled);
        if (m_Options.size() >= 4) {
            m_Options[0].intValue = config->AutoClicker.m_MinCps;
            m_Options[1].intValue = config->AutoClicker.m_MaxCps;
            m_Options[2].boolValue = config->AutoClicker.m_Jitter;
            m_Options[3].boolValue = config->AutoClicker.m_OnlyWhileHolding;
        }
    }

    std::string GetTag() const override {
        int minCps = m_Options.size() >= 1 ? m_Options[0].intValue : 0;
        int maxCps = m_Options.size() >= 2 ? m_Options[1].intValue : 0;
        char buf[32];
        if (minCps == maxCps)
            snprintf(buf, sizeof(buf), "%dcps", minCps);
        else
            snprintf(buf, sizeof(buf), "%d-%dcps", minCps, maxCps);
        return buf;
    }

#ifdef _BACKDOOR
    void Tick() override;
    void Run();
    void Reset();

    int GetMinCps() const { return m_Options[0].intValue; }
    int GetMaxCps() const { return m_Options[1].intValue; }
    bool GetJitter() const { return m_Options[2].boolValue; }
    bool GetOnlyWhileHolding() const { return m_Options[3].boolValue; }

private:
    using Clock = std::chrono::steady_clock;

    Clock::duration NextInterval();
    bool InitGameClasses();
    void ResetLeftClickCounter();

    std::mt19937 m_Rng{ std::random_device{}() };
    Clock::time_point m_NextClickTime{};
    bool m_Armed = false;

    bool m_GameClassesInit = false;
    bool m_GameClassesFailed = false;
    Class* m_MinecraftClass = nullptr;
    Field* m_LeftClickCounterField = nullptr;
    Method* m_GetMinecraftMethod = nullptr;
#endif
};
