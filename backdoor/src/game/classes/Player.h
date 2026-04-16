#pragma once

#include <string>
#include <jni.h>

class Player {
public:
    std::string GetName(JNIEnv* env, bool stripFormatting = true);
    float GetMaxHealth(JNIEnv* env);
    float GetRealHealth(JNIEnv* env);
    jobject GetHeldItem(JNIEnv* env);
    jobject GetInventoryPlayer(JNIEnv* env);
    jobject GetInventoryContainer(JNIEnv* env);
    jobject GetCurrentArmor(int slot, JNIEnv* env);
    bool IsUsingItem(JNIEnv* env);
    jobject GetActivePotionEffect(int potionId, JNIEnv* env);
    void SendPacket(jobject packet, JNIEnv* env);
    void SetJumpTicks(int ticks, JNIEnv* env);
};
