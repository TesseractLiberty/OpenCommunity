#pragma once

#include <jni.h>

class Slot {
public:
    jobject GetStack(JNIEnv* env);
};
