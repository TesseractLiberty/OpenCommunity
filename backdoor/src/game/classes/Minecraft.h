#pragma once

#include <jni.h>

class Minecraft {
public:
    static jobject GetTheMinecraft(JNIEnv* env);
    static jobject GetThePlayer(JNIEnv* env);
    static jobject GetCurrentScreen(JNIEnv* env);
    static jobject GetPlayerController(JNIEnv* env);
};
