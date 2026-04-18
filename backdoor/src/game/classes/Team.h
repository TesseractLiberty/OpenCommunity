#pragma once

#include <jni.h>
#include <string>

class Team {
public:
    std::string GetColorPrefix(JNIEnv* env);
    std::string GetColorSuffix(JNIEnv* env);
    std::string GetRegisteredName(JNIEnv* env);
    jobject GetNameTagVisibility(JNIEnv* env);
    void SetNameTagVisibility(JNIEnv* env, jobject visibilityObject);
    bool IsSameTeam(JNIEnv* env, Team* other);
};
