#pragma once

#include <jni.h>
#include <string>

class ItemStack {
public:
    jobject GetItem(JNIEnv* env);
    std::string GetDisplayName(JNIEnv* env);
    bool IsArmor(JNIEnv* env);
    bool IsAppleGold(JNIEnv* env);
    int GetMetadata(JNIEnv* env);
    int GetMaxDamage(JNIEnv* env);
    int GetItemDamage(JNIEnv* env);
};
