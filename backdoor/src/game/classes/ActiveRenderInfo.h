#pragma once

#include <jni.h>
#include <vector>

class ActiveRenderInfo {
public:
    static std::vector<float> GetProjection(JNIEnv* env);
    static std::vector<float> GetModelView(JNIEnv* env);
};
