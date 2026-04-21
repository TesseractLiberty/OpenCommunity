#pragma once

#include "Player.h"

#include <jni.h>

class RenderManager {
public:
    Vec3D GetRenderPos(JNIEnv* env);
};
