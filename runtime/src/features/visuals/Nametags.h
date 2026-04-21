#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/images/modules/tag_icon.h"

#ifdef _RUNTIME
#include "../../game/classes/Player.h"
#include "../../../../deps/imgui/fonts/fonts.hpp"
#include "../../../../deps/imgui/imgui.h"
#endif

class Nametags : public Module {
public:
    MODULE_INFO(Nametags, "Nametags", "Renders name tags with real health and distance.", ModuleCategory::Visuals) {
        SetBeta();
        SetImagePrefix(module_icons::tag_icon_data, module_icons::tag_icon_size);
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        config->Nametags.m_Enabled = IsEnabled();
        config->Modules.m_Nametags = IsEnabled();
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        SetEnabled(config->Nametags.m_Enabled);
    }

#ifdef _RUNTIME
public:
    static void SetFont(ImFont* font);

    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;
    void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) override;

private:
    static ImFont* s_SanFranciscoBoldFont;
#endif
};
