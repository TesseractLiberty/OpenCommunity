#pragma once

#include <jni.h>

class Container {
public:
    int GetWindowId(JNIEnv* env);
    jobject GetSlot(int slotId, JNIEnv* env);
};
