#pragma once

#include <jni.h>

class ItemStack {
public:
    bool IsArmor(JNIEnv* env);
    int GetMaxDamage(JNIEnv* env);
    int GetItemDamage(JNIEnv* env);

private:
    jobject GetItem(JNIEnv* env);
};
