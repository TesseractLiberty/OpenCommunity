#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/enemy_info_list_icon.h"

#ifdef _RUNTIME
#include "../../game/classes/Player.h"
#include <jni.h>
#endif

class EnemyInfoList : public Module {
public:
    MODULE_INFO(EnemyInfoList, "EnemyInfoList", "Tracks enemy clan members and sends their info to the application.", ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::enemy_info_list_icon_data, module_icons::enemy_info_list_icon_data_size);
        AddOption(ModuleOption::Button("Open in new Application", "Open in new Application"));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->EnemyInfoList.m_Enabled = IsEnabled();
        config->Modules.m_EnemyInfoList = IsEnabled();

        if (!m_Options.empty() && m_Options[0].buttonPressed) {
            if (config->EnemyInfoList.m_SecondApplicationOpen) {
                config->EnemyInfoList.m_FocusSecondApplicationRequested = true;
            } else {
                config->EnemyInfoList.m_OpenSecondApplicationRequested = true;
            }
            m_Options[0].buttonPressed = false;
        }
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

            SetEnabled(config->EnemyInfoList.m_Enabled);
            if (!m_Options.empty()) {
                m_Options[0].buttonLabel = config->EnemyInfoList.m_SecondApplicationOpen
                    ? "Openned in a second application, click to view"
                    : "Open in new Application";
            }
        }

#ifdef _RUNTIME
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void ShutdownRuntime(void* envPtr) override;
    std::string GetTag() const override;

    static void OnLocalAttack(JNIEnv* env, Player* attackedPlayer);
#endif

private:
#ifdef _RUNTIME
    int m_PreviousSwingProgressInt = 0;
    bool m_PreviousPhysicalClick = false;
    bool m_WasEnabled = false;
#endif
};
