#pragma once

#include "FeatureManager.h"

#include "../../backdoor/src/features/combat/AutoClicker.h"
#include "../../backdoor/src/features/combat/ArmorFilter.h"
#include "../../backdoor/src/features/combat/ArmorSwap.h"
#include "../../backdoor/src/features/combat/AutoGapple.h"
#include "../../backdoor/src/features/combat/NoHitDelay.h"
#include "../../backdoor/src/features/combat/Target.h"
#include "../../backdoor/src/features/combat/HideClans.h"
#include "../../backdoor/src/features/movement/NoJumpDelay.h"
#include "../../backdoor/src/features/visuals/ArrayList.h"
#include "../../backdoor/src/features/visuals/DamageIndicator.h"

inline void RegisterAllModules() {
    auto* fm = FeatureManager::Get();

    fm->RegisterModule(std::make_shared<AutoClicker>());
    fm->RegisterModule(std::make_shared<ArmorFilter>());
    fm->RegisterModule(std::make_shared<ArmorSwap>());
    fm->RegisterModule(std::make_shared<AutoGapple>());
    fm->RegisterModule(std::make_shared<NoHitDelay>());
    fm->RegisterModule(std::make_shared<Target>());
    fm->RegisterModule(std::make_shared<HideClans>());
    fm->RegisterModule(std::make_shared<NoJumpDelay>());
    fm->RegisterModule(std::make_shared<ArrayList>());
    fm->RegisterModule(std::make_shared<DamageIndicator>());
}
