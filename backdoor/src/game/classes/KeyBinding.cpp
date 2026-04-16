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

    Class* keyBindingClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/settings/KeyBinding"));
    if (!keyBindingClass) {
        return 0;
    }

    const char* possibleFields[] = {
        Mapper::Get("keyCode").c_str(),
        "g",
        "field_74512_d"
    };

    for (const char* fieldName : possibleFields) {
        if (!fieldName || !fieldName[0]) {
            continue;
        }

        Field* field = keyBindingClass->GetField(env, fieldName, "I");
        if (field) {
            return field->GetIntField(env, this);
        }
    }

    return 0;
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

    const char* possibleMethods[] = {
        Mapper::Get("setKeyBindState").c_str(),
        "func_74510_a",
        "a"
    };

    jmethodID methodId = nullptr;
    for (const char* methodName : possibleMethods) {
        if (!methodName || !methodName[0]) {
            continue;
        }

        methodId = env->GetStaticMethodID(reinterpret_cast<jclass>(keyBindingClass), methodName, "(IZ)V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            methodId = nullptr;
        }

        if (methodId) {
            break;
        }
    }

    if (methodId) {
        env->CallStaticVoidMethod(reinterpret_cast<jclass>(keyBindingClass), methodId, keyCode, pressed);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
}
