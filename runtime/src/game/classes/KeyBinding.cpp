#include "pch.h"
#include "KeyBinding.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

int KeyBinding::GetKeyCode(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return 0;
    }

    const std::string className = Mapper::Get("net/minecraft/client/settings/KeyBinding");
    const std::string fieldName = Mapper::Get("keyCode");
    if (className.empty() || fieldName.empty()) {
        return 0;
    }

    Class* keyBindingClass = g_Game->FindClass(className);
    if (!keyBindingClass) {
        return 0;
    }

    Field* field = keyBindingClass->GetField(env, fieldName.c_str(), "I");
    return field ? field->GetIntField(env, this) : 0;
}

void KeyBinding::SetKeyBindState(int keyCode, bool pressed, JNIEnv* env) {
    if (!env || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    const std::string className = Mapper::Get("net/minecraft/client/settings/KeyBinding");
    if (className.empty()) {
        return;
    }

    Class* keyBindingClass = g_Game->FindClass(className);
    if (!keyBindingClass) {
        return;
    }

    const std::string methodName = Mapper::Get("setKeyBindState");
    if (methodName.empty()) {
        return;
    }

    Method* method = keyBindingClass->GetMethod(env, methodName.c_str(), "(IZ)V", true);
    if (method) {
        method->CallVoidMethod(env, keyBindingClass, true, keyCode, pressed);
    }
}
