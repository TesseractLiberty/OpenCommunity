#pragma once

#include <jni.h>

class RenderHelper {
public:
    static void EnableGUIStandardItemLighting(JNIEnv* env);
    static void DisableStandardItemLighting(JNIEnv* env);
};
