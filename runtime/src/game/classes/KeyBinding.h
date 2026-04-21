#pragma once

#include <jni.h>

class KeyBinding {
public:
    int GetKeyCode(JNIEnv* env);
    static void SetKeyBindState(int keyCode, bool pressed, JNIEnv* env);
};
