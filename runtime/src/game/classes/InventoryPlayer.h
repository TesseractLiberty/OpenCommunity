#pragma once

#include <jni.h>

class InventoryPlayer {
public:
    jobject GetStackInSlot(int slot, JNIEnv* env);
    void SetCurrentItem(int slot, JNIEnv* env);
    int GetCurrentItem(JNIEnv* env);
};
