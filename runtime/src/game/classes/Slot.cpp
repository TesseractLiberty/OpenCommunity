#include "pch.h"
#include "Slot.h"

#include "../jni/Class.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

jobject Slot::GetStack(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return nullptr;
    }

    const std::string className = Mapper::Get("net/minecraft/inventory/Slot");
    const std::string methodName = Mapper::Get("getStack");
    const std::string signature = Mapper::Get("net/minecraft/item/ItemStack", 3);
    if (className.empty() || methodName.empty() || signature.empty()) {
        return nullptr;
    }

    Class* slotClass = g_Game->FindClass(className);
    Method* method = slotClass ? slotClass->GetMethod(env, methodName.c_str(), signature.c_str()) : nullptr;
    return method ? method->CallObjectMethod(env, this) : nullptr;
}
