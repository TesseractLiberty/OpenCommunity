#include "pch.h"
#include "Player.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../mapping/Mapper.h"

jobject Player::GetInventoryContainer(JNIEnv* env) {
    if (!env || !this) {
        return nullptr;
    }

    auto* playerClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!playerClass) {
        return nullptr;
    }

    const std::string fieldName = Mapper::Get("inventoryContainer");
    const std::string fieldSignature = Mapper::Get("net/minecraft/inventory/Container", 2);
    Field* field = nullptr;
    if (!fieldName.empty() && !fieldSignature.empty()) {
        field = playerClass->GetField(env, fieldName.c_str(), fieldSignature.c_str());
    }

    jobject value = field ? field->GetObjectField(env, this) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}
