#pragma once

#include <jni.h>

struct AxisAlignedBB_t {
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
};

class AxisAlignedBB {
public:
    AxisAlignedBB_t GetNativeBoundingBox(JNIEnv* env);
    void SetNativeBoundingBox(AxisAlignedBB_t buffer, JNIEnv* env);
};
