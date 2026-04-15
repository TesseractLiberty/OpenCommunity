#pragma once

#include "FeatureManager.h"

#include "../../backdoor/src/features/combat/AutoClicker.h"
#include "../../backdoor/src/features/visuals/ArrayList.h"

inline void RegisterAllModules() {
    auto* fm = FeatureManager::Get();

    fm->RegisterModule(std::make_shared<AutoClicker>());
    fm->RegisterModule(std::make_shared<ArrayList>());
}
