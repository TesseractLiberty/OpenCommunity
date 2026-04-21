#include "pch.h"
#include "ItemArmor.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../mapping/Mapper.h"

namespace {
    Class* GetItemArmorClass() {
        if (!g_Game || !g_Game->IsInitialized()) {
            return nullptr;
        }

        const std::string className = Mapper::Get("net/minecraft/item/ItemArmor");
        return className.empty() ? nullptr : g_Game->FindClass(className);
    }
}

int ItemArmor::GetArmorType(jobject item, JNIEnv* env) {
    if (!item || !env) {
        return -1;
    }

    Class* armorClass = GetItemArmorClass();
    if (!armorClass) {
        return -1;
    }

    Field* field = armorClass->GetField(env, Mapper::Get("armorType").c_str(), "I");
    return field ? field->GetIntField(env, item) : -1;
}

int ItemArmor::GetDamageReduceAmount(jobject item, JNIEnv* env) {
    if (!item || !env) {
        return 0;
    }

    Class* armorClass = GetItemArmorClass();
    if (!armorClass) {
        return 0;
    }

    Field* field = armorClass->GetField(env, Mapper::Get("damageReduceAmount").c_str(), "I");
    return field ? field->GetIntField(env, item) : 0;
}
