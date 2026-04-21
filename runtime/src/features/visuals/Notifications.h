#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/notifications_icon.h"

#include <string>

#ifdef _RUNTIME
#include "../../../../deps/imgui/imgui.h"
#endif

#ifdef ERROR
#undef ERROR
#endif

class Notifications : public Module {
public:
    enum class Severity {
        Success,
        Error,
        Info,
        Enabled,
        Disabled
    };

    struct SendNotifications {
        static void SUCCESS(const std::string& message, const std::string& title = "Success");
        static void ERROR(const std::string& message, const std::string& title = "Error");
        static void INFO(const std::string& message, const std::string& title = "Info");
        static void ENABLED(const std::string& moduleName, const std::string& title = "Module");
        static void DISABLED(const std::string& moduleName, const std::string& title = "Module");
    };

    MODULE_INFO(Notifications, "Notifications", "Displays HUD notifications.", ModuleCategory::Visuals) {
        SetImagePrefix(module_icons::notifications_icon_data, module_icons::notifications_icon_data_size);
        SetEnabled(true);
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->Notifications.m_Enabled = IsEnabled();
        config->Modules.m_Notifications = IsEnabled();
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->Notifications.m_Enabled);
    }

    static void SendNotification(Severity severity, const std::string& title, const std::string& message);
    static void SendSuccess(const std::string& message, const std::string& title = "Success");
    static void SendError(const std::string& message, const std::string& title = "Error");
    static void SendInfo(const std::string& message, const std::string& title = "Info");
    static void SendEnabled(const std::string& moduleName, const std::string& title = "Module");
    static void SendDisabled(const std::string& moduleName, const std::string& title = "Module");

#ifdef _RUNTIME
    static void SetFonts(ImFont* regular, ImFont* bold);
    void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) override;
#endif
};
