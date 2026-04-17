#pragma once

#include <jni.h>
#include <string>

class Team {
public:
    std::string GetColorPrefix(JNIEnv* env);
    std::string GetColorSuffix(JNIEnv* env);
    std::string GetRegisteredName(JNIEnv* env);
    bool IsSameTeam(JNIEnv* env, Team* other);
};
