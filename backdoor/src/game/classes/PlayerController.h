#pragma once

#include <jni.h>

class PlayerController {
public:
    static void WindowClick(jobject player, int windowId, int slotId, int mouseButtonClicked, int mode, JNIEnv* env);
};
