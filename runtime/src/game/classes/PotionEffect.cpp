#include "pch.h"
#include "PotionEffect.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

int PotionEffect::GetDuration(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return 0;
    }

    const std::string className = Mapper::Get("net/minecraft/potion/PotionEffect");
    if (className.empty()) {
        return 0;
    }

    Class* effectClass = g_Game->FindClass(className);
    if (!effectClass) {
        return 0;
    }

    const std::string methodName = Mapper::Get("getDuration");
    Method* method = methodName.empty() ? nullptr : effectClass->GetMethod(env, methodName.c_str(), "()I");
    if (method) {
        return method->CallIntMethod(env, this);
    }

    const std::string fieldName = Mapper::Get("duration");
    Field* field = fieldName.empty() ? nullptr : effectClass->GetField(env, fieldName.c_str(), "I");
    if (field) {
        return field->GetIntField(env, this);
    }

    field = effectClass->GetField(env, "c", "I");
    return field ? field->GetIntField(env, this) : 0;
}
