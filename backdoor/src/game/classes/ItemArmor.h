#pragma once

#include <jni.h>

class ItemArmor {
public:
    static int GetArmorType(jobject item, JNIEnv* env);
    static int GetDamageReduceAmount(jobject item, JNIEnv* env);
};
