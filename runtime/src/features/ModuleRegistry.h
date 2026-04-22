#pragma once

#include "../../../shared/common/modules/ModuleManager.h"

#include "combat/AutoClicker.h"
#include "combat/ArmorFilter.h"
#include "combat/ArmorSwap.h"
#include "combat/AutoGapple.h"
#include "combat/NoHitDelay.h"
#include "movement/NoJumpDelay.h"
#include "visuals/ArrayList.h"
#include "visuals/DamageIndicator.h"
#include "visuals/Nametags.h"
#include "visuals/Notifications.h"
#include "visuals/ItemChams.h"
#include "visuals/NoRender.h"
#include "visuals/Target.h"
#include "visuals/HideClans.h"

#include <memory>

namespace ModuleRegistry {
    inline void RegisterAll(ModuleManager& modules) {
        modules.RegisterModule(std::make_shared<AutoClicker>());
        modules.RegisterModule(std::make_shared<ArmorFilter>());
        modules.RegisterModule(std::make_shared<ArmorSwap>());
        modules.RegisterModule(std::make_shared<AutoGapple>());
        modules.RegisterModule(std::make_shared<NoHitDelay>());
        modules.RegisterModule(std::make_shared<Target>());
        modules.RegisterModule(std::make_shared<HideClans>());
        modules.RegisterModule(std::make_shared<NoJumpDelay>());
        modules.RegisterModule(std::make_shared<ArrayList>());
        modules.RegisterModule(std::make_shared<DamageIndicator>());
        modules.RegisterModule(std::make_shared<Nametags>());
        modules.RegisterModule(std::make_shared<Notifications>());
        modules.RegisterModule(std::make_shared<ItemChams>());
        modules.RegisterModule(std::make_shared<NoRender>());
    }

    inline void RegisterAll() {
        RegisterAll(*ModuleManager::Get());
    }
}
