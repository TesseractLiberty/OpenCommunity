#include "pch.h"
#include "InventoryPlayer.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

jobject InventoryPlayer::GetStackInSlot(int slot, JNIEnv* env) {
    if (!env || !this) {
        return nullptr;
    }

    const auto inventoryClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!inventoryClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getStackInSlot");
    const std::string signature = "(I)" + Mapper::Get("net/minecraft/item/ItemStack", 2);
    Method* method = (methodName.empty() || signature.empty()) ? nullptr : inventoryClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject value = method ? method->CallObjectMethod(env, this, false, slot) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(inventoryClass));
    return value;
}

void InventoryPlayer::SetCurrentItem(int slot, JNIEnv* env) {
    if (!env || !this) {
        return;
    }

    const auto inventoryClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!inventoryClass) {
        return;
    }

    const std::string fieldName = Mapper::Get("currentItem");
    Field* field = fieldName.empty() ? nullptr : inventoryClass->GetField(env, fieldName.c_str(), "I");
    if (field) {
        field->SetIntField(env, this, slot);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(inventoryClass));
}

int InventoryPlayer::GetCurrentItem(JNIEnv* env) {
    if (!env || !this) {
        return 0;
    }

    const auto inventoryClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!inventoryClass) {
        return 0;
    }

    const std::string fieldName = Mapper::Get("currentItem");
    Field* field = fieldName.empty() ? nullptr : inventoryClass->GetField(env, fieldName.c_str(), "I");
    const int value = field ? field->GetIntField(env, this) : 0;
    env->DeleteLocalRef(reinterpret_cast<jclass>(inventoryClass));
    return value;
}
