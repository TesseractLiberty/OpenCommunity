#pragma once

#include <jni.h>

class RenderItem {
public:
    void RenderItemIntoGUI(jobject itemStack, int x, int y, JNIEnv* env);
};
