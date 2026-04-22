#pragma once

#include <jni.h>

class EntityItem {
public:
    jobject GetItemStack(JNIEnv* env);
};
