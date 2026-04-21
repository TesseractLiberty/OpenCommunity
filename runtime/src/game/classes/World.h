#pragma once

#include <jni.h>
#include <vector>

class World {
public:
    std::vector<class Player*> GetPlayerEntities(JNIEnv* env);
    jobject GetScoreboard(JNIEnv* env);
};
