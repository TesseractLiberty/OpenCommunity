#include "pch.h"
#include "RenderManager.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../mapping/Mapper.h"

Vec3D RenderManager::GetRenderPos(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return {};
    }

    const std::string className = Mapper::Get("net/minecraft/client/renderer/entity/RenderManager");
    if (className.empty()) {
        return {};
    }

    Class* renderManagerClass = g_Game->FindClass(className);
    if (!renderManagerClass) {
        return {};
    }

    Field* renderPosX = renderManagerClass->GetField(env, Mapper::Get("renderPosX").c_str(), "D");
    Field* renderPosY = renderManagerClass->GetField(env, Mapper::Get("renderPosY").c_str(), "D");
    Field* renderPosZ = renderManagerClass->GetField(env, Mapper::Get("renderPosZ").c_str(), "D");
    if (!renderPosX || !renderPosY || !renderPosZ) {
        return {};
    }

    return Vec3D{
        renderPosX->GetDoubleField(env, this),
        renderPosY->GetDoubleField(env, this),
        renderPosZ->GetDoubleField(env, this)
    };
}
