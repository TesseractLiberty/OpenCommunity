#pragma once

#include <jni.h>

class Minecraft {
public:
    static jobject GetTheMinecraft(JNIEnv* env);
    static jobject GetThePlayer(JNIEnv* env);
    static jobject GetTheWorld(JNIEnv* env);
    static jobject GetCurrentScreen(JNIEnv* env);
    static jobject GetPlayerController(JNIEnv* env);
    static jobject GetObjectMouseOver(JNIEnv* env);
    static jobject GetRenderItem(JNIEnv* env);
    static jobject GetRenderManager(JNIEnv* env);
    static jobject GetNetHandler(JNIEnv* env);
    static jobject GetTimer(JNIEnv* env);
    static jobject GetKeyBindUseItem(JNIEnv* env);
    static void DisplayGuiScreen(jobject guiScreen, JNIEnv* env);
    static jobject CreateGuiInventory(jobject player, JNIEnv* env);
    static void SetLeftClickCounter(int value, JNIEnv* env);
    static void SetRightClickCounter(int value, JNIEnv* env);
};
