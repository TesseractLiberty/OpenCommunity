#pragma once

#include <string>
#include <jni.h>

struct Vec3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

class Player {
public:
    std::string GetName(JNIEnv* env, bool stripFormatting = true);
    std::string GetFormattedDisplayName(JNIEnv* env);
    std::string GetClanTag(JNIEnv* env, class Scoreboard* scoreboard);
    std::string GetFormattedClanTag(JNIEnv* env, class Scoreboard* scoreboard);
    float GetHealth(JNIEnv* env);
    float GetMaxHealth(JNIEnv* env);
    float GetRealHealth(JNIEnv* env);
    Vec3D GetPos(JNIEnv* env);
    Vec3D GetLastTickPos(JNIEnv* env);
    float GetRotationPitch(JNIEnv* env);
    float GetRotationYaw(JNIEnv* env);
    float GetPrevRotationPitch(JNIEnv* env);
    float GetPrevRotationYaw(JNIEnv* env);
    jobject GetHeldItem(JNIEnv* env);
    jobject GetInventoryPlayer(JNIEnv* env);
    jobject GetInventoryContainer(JNIEnv* env);
    jobject GetCurrentArmor(int slot, JNIEnv* env);
    float GetDistanceToEntity(jobject entity, JNIEnv* env);
    float GetEyeHeight(JNIEnv* env);
    float GetWidth(JNIEnv* env);
    float GetHeight(JNIEnv* env);
    void SetWidth(float value, JNIEnv* env);
    void SetHeight(float value, JNIEnv* env);
    void SetPosition(double x, double y, double z, JNIEnv* env);
    void SetAlwaysRenderNameTag(bool value, JNIEnv* env);
    bool IsInvisible(JNIEnv* env);
    bool IsInWeb(JNIEnv* env);
    int GetHurtTime(JNIEnv* env);
    bool IsUsingItem(JNIEnv* env);
    int GetSwingProgressInt(JNIEnv* env);
    jobject GetActivePotionEffect(int potionId, JNIEnv* env);
    jobject GetBoundingBox(JNIEnv* env);
    bool HasZeroedBoundingBox(JNIEnv* env);
    void Zero(JNIEnv* env);
    void Restore(JNIEnv* env);
    void SendPacket(jobject packet, JNIEnv* env);
    void SetJumpTicks(int ticks, JNIEnv* env);
};
