#include "pch.h"
#include "NoJumpDelay.h"

void NoJumpDelay::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (env) {
        Run(env);
    }
}

void NoJumpDelay::Run(JNIEnv* env) {
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
        auto* player = reinterpret_cast<Player*>(playerObject);
        player->SetJumpTicks(0, env);
        MarkInUse(100);
        env->DeleteLocalRef(playerObject);
    }

    env->DeleteLocalRef(worldObject);
}
