#include "pch.h"
#include "ItemStack.h"

#include "../jni/Class.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

jobject ItemStack::GetItem(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return nullptr;
    }

    const std::string className = Mapper::Get("net/minecraft/item/ItemStack");
    const std::string methodName = Mapper::Get("getItem");
    const std::string signature = Mapper::Get("net/minecraft/item/Item", 3);
    if (className.empty() || methodName.empty() || signature.empty()) {
        return nullptr;
    }

    Class* itemStackClass = g_Game->FindClass(className);
    Method* method = itemStackClass ? itemStackClass->GetMethod(env, methodName.c_str(), signature.c_str()) : nullptr;
    return method ? method->CallObjectMethod(env, this) : nullptr;
}

bool ItemStack::IsArmor(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return false;
    }

    jobject item = GetItem(env);
    if (!item) {
        return false;
    }

    const std::string className = Mapper::Get("net/minecraft/item/ItemArmor");
    Class* armorClass = className.empty() ? nullptr : g_Game->FindClass(className);
    const bool isArmor = armorClass && env->IsInstanceOf(item, reinterpret_cast<jclass>(armorClass));
    env->DeleteLocalRef(item);
    return isArmor;
}

int ItemStack::GetMaxDamage(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return 0;
    }

    const std::string className = Mapper::Get("net/minecraft/item/ItemStack");
    const std::string methodName = Mapper::Get("getMaxDamage");
    if (className.empty() || methodName.empty()) {
        return 0;
    }

    Class* itemStackClass = g_Game->FindClass(className);
    Method* method = itemStackClass ? itemStackClass->GetMethod(env, methodName.c_str(), "()I") : nullptr;
    return method ? method->CallIntMethod(env, this) : 0;
}

int ItemStack::GetItemDamage(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return 0;
    }

    const std::string className = Mapper::Get("net/minecraft/item/ItemStack");
    const std::string methodName = Mapper::Get("getItemDamage");
    if (className.empty() || methodName.empty()) {
        return 0;
    }

    Class* itemStackClass = g_Game->FindClass(className);
    Method* method = itemStackClass ? itemStackClass->GetMethod(env, methodName.c_str(), "()I") : nullptr;
    return method ? method->CallIntMethod(env, this) : 0;
}
