#include "pch.h"
#include "World.h"

#include "../jni/Class.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

jobject World::GetScoreboard(JNIEnv* env) {
    if (!env || !this) {
        return nullptr;
    }

    auto* worldClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!worldClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getScoreboard");
    const std::string signature = "()" + Mapper::Get("net/minecraft/scoreboard/Scoreboard", 2);
    Method* method = (methodName.empty() || signature.empty()) ? nullptr : worldClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject value = method ? method->CallObjectMethod(env, this) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(worldClass));
    return value;
}
