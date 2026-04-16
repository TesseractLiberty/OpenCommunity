#include "pch.h"
#include "NoHitDelay.h"

void NoHitDelay::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (env) {
        Run(env);
    }
}

void NoHitDelay::Run(JNIEnv* env) {
    if (!env || !IsEnabled()) {
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        return;
    }

    jobject currentScreenObject = Minecraft::GetCurrentScreen(env);
    if (currentScreenObject) {
        env->DeleteLocalRef(currentScreenObject);
        env->DeleteLocalRef(worldObject);
        return;
    }

    jobject playerObject = Minecraft::GetThePlayer(env);
    if (playerObject) {
        Minecraft::SetLeftClickCounter(0, env);
        env->DeleteLocalRef(playerObject);
    }

    env->DeleteLocalRef(worldObject);
}
