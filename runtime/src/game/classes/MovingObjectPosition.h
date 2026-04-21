#pragma once

#include <jni.h>

class MovingObjectPosition {
public:
    bool IsAimingEntity(JNIEnv* env);
    jobject GetEntity(JNIEnv* env);
};
