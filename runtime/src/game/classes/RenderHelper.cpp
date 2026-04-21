#include "pch.h"
#include "RenderHelper.h"

#include "../jni/Class.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

void RenderHelper::EnableGUIStandardItemLighting(JNIEnv* env) {
    if (!env || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    const std::string className = Mapper::Get("net/minecraft/client/renderer/RenderHelper");
    const std::string methodName = Mapper::Get("enableGUIStandardItemLighting");
    if (className.empty() || methodName.empty()) {
        return;
    }

    Class* renderHelperClass = g_Game->FindClass(className);
    Method* method = renderHelperClass ? renderHelperClass->GetMethod(env, methodName.c_str(), "()V", true) : nullptr;
    if (method) {
        method->CallVoidMethod(env, renderHelperClass, true);
    }
}

void RenderHelper::DisableStandardItemLighting(JNIEnv* env) {
    if (!env || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    const std::string className = Mapper::Get("net/minecraft/client/renderer/RenderHelper");
    const std::string methodName = Mapper::Get("disableStandardItemLighting");
    if (className.empty() || methodName.empty()) {
        return;
    }

    Class* renderHelperClass = g_Game->FindClass(className);
    Method* method = renderHelperClass ? renderHelperClass->GetMethod(env, methodName.c_str(), "()V", true) : nullptr;
    if (method) {
        method->CallVoidMethod(env, renderHelperClass, true);
    }
}
