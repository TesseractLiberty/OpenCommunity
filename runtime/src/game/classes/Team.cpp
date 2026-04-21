#include "pch.h"
#include "Team.h"

#include "../jni/Class.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

namespace {
    std::string ReadStringResult(JNIEnv* env, jobject owner, const char* methodName) {
        if (!env || !owner || !methodName || !methodName[0]) {
            return {};
        }

        auto* teamClass = reinterpret_cast<Class*>(env->GetObjectClass(owner));
        if (!teamClass) {
            return {};
        }

        Method* method = teamClass->GetMethod(env, methodName, "()Ljava/lang/String;");
        std::string result;
        if (method) {
            jstring value = static_cast<jstring>(method->CallObjectMethod(env, owner));
            if (value) {
                const char* chars = env->GetStringUTFChars(value, nullptr);
                if (chars) {
                    result = chars;
                    env->ReleaseStringUTFChars(value, chars);
                }
                env->DeleteLocalRef(value);
            }
        }

        env->DeleteLocalRef(reinterpret_cast<jclass>(teamClass));
        return result;
    }
}

std::string Team::GetColorPrefix(JNIEnv* env) {
    return ReadStringResult(env, reinterpret_cast<jobject>(this), Mapper::Get("getColorPrefix").c_str());
}

std::string Team::GetColorSuffix(JNIEnv* env) {
    return ReadStringResult(env, reinterpret_cast<jobject>(this), Mapper::Get("getColorSuffix").c_str());
}

std::string Team::GetRegisteredName(JNIEnv* env) {
    return ReadStringResult(env, reinterpret_cast<jobject>(this), Mapper::Get("getRegisteredName").c_str());
}

jobject Team::GetNameTagVisibility(JNIEnv* env) {
    if (!env || !this) {
        return nullptr;
    }

    auto* teamClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!teamClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getNameTagVisibility");
    const std::string visibilitySignature = Mapper::Get("net/minecraft/scoreboard/Team$EnumVisible", 2);
    const std::string signature = "()" + visibilitySignature;
    Method* method = (methodName.empty() || visibilitySignature.empty()) ? nullptr : teamClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject visibility = method ? method->CallObjectMethod(env, this) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(teamClass));
    return visibility;
}

void Team::SetNameTagVisibility(JNIEnv* env, jobject visibilityObject) {
    if (!env || !this || !visibilityObject) {
        return;
    }

    auto* teamClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!teamClass) {
        return;
    }

    const std::string methodName = Mapper::Get("setNameTagVisibility");
    const std::string visibilitySignature = Mapper::Get("net/minecraft/scoreboard/Team$EnumVisible", 2);
    const std::string signature = "(" + visibilitySignature + ")V";
    Method* method = (methodName.empty() || visibilitySignature.empty()) ? nullptr : teamClass->GetMethod(env, methodName.c_str(), signature.c_str());
    if (method) {
        method->CallVoidMethod(env, this, false, visibilityObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(teamClass));
}

bool Team::IsSameTeam(JNIEnv* env, Team* other) {
    if (!env || !this || !other) {
        return false;
    }

    auto* teamClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!teamClass) {
        return false;
    }

    const std::string teamSignature = Mapper::Get("net/minecraft/scoreboard/Team", 2);
    Method* method = teamSignature.empty()
        ? nullptr
        : teamClass->GetMethod(env, Mapper::Get("isSameTeam").c_str(), ("(" + teamSignature + ")Z").c_str());
    const bool sameTeam = method ? method->CallBooleanMethod(env, this, false, other) : false;
    env->DeleteLocalRef(reinterpret_cast<jclass>(teamClass));
    return sameTeam;
}
