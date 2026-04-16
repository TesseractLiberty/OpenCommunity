#pragma once

#include <jni.h>

class World {
public:
    jobject GetScoreboard(JNIEnv* env);
};
