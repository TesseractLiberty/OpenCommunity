#include "pch.h"
#include "Container.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

int Container::GetWindowId(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return 0;
    }

    const std::string className = Mapper::Get("net/minecraft/inventory/Container");
    const std::string fieldName = Mapper::Get("windowId");
    if (className.empty() || fieldName.empty()) {
        return 0;
    }

    Class* containerClass = g_Game->FindClass(className);
    Field* field = containerClass ? containerClass->GetField(env, fieldName.c_str(), "I") : nullptr;
    return field ? field->GetIntField(env, this) : 0;
}

jobject Container::GetSlot(int slotId, JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return nullptr;
    }

    const std::string className = Mapper::Get("net/minecraft/inventory/Container");
    const std::string methodName = Mapper::Get("getSlot");
    const std::string slotSignature = Mapper::Get("net/minecraft/inventory/Slot", 2);
    if (className.empty() || methodName.empty() || slotSignature.empty()) {
        return nullptr;
    }

    Class* containerClass = g_Game->FindClass(className);
    if (!containerClass) {
        return nullptr;
    }

    const std::string signature = "(I)" + slotSignature;
    Method* method = containerClass->GetMethod(env, methodName.c_str(), signature.c_str());
    return method ? method->CallObjectMethod(env, this, false, slotId) : nullptr;
}
