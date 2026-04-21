#pragma once

#include <jni.h>

class PacketClientStatus {
public:
    static jobject Create(JNIEnv* env, int stateOrdinal);
};
