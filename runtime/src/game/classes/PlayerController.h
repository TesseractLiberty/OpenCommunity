#pragma once

#include <jni.h>

class PlayerController {
public:
    static void WindowClick(jobject player, int windowId, int slotId, int mouseButtonClicked, int mode, JNIEnv* env);
    static bool SendUseItem(jobject player, jobject world, jobject stack, JNIEnv* env);
    static void UpdateController(JNIEnv* env);
};
