#include "pch.h"
#include "Minecraft.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../mapping/Mapper.h"

namespace {
    Class* GetMinecraftClass() {
        if (!g_Game || !g_Game->IsInitialized()) {
            return nullptr;
        }

        const std::string className = Mapper::Get("net/minecraft/client/Minecraft");
        if (className.empty()) {
            return nullptr;
        }

        return g_Game->FindClass(className);
    }

    jobject GetMinecraftMember(JNIEnv* env, const char* fieldName, const char* fieldSignature) {
        if (!env) {
            return nullptr;
        }

        Class* minecraftClass = GetMinecraftClass();
        if (!minecraftClass) {
            return nullptr;
        }

        Field* field = minecraftClass->GetField(env, fieldName, fieldSignature);
        if (!field) {
            return nullptr;
        }

        jobject minecraft = Minecraft::GetTheMinecraft(env);
        if (!minecraft) {
            return nullptr;
        }

        jobject value = field->GetObjectField(env, minecraft);
        env->DeleteLocalRef(minecraft);
        return value;
    }
}

jobject Minecraft::GetTheMinecraft(JNIEnv* env) {
    if (!env) {
        return nullptr;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return nullptr;
    }

    const std::string fieldName = Mapper::Get("theMinecraft");
    const std::string fieldSignature = Mapper::Get("net/minecraft/client/Minecraft", 2);
    if (fieldName.empty() || fieldSignature.empty()) {
        return nullptr;
    }

    Field* field = minecraftClass->GetField(env, fieldName.c_str(), fieldSignature.c_str(), true);
    if (!field) {
        return nullptr;
    }

    return field->GetObjectField(env, minecraftClass, true);
}

jobject Minecraft::GetThePlayer(JNIEnv* env) {
    const std::string fieldName = Mapper::Get("thePlayer");
    const std::string fieldSignature = Mapper::Get("net/minecraft/client/entity/EntityClientPlayerMP", 2);
    if (fieldName.empty() || fieldSignature.empty()) {
        return nullptr;
    }

    return GetMinecraftMember(env, fieldName.c_str(), fieldSignature.c_str());
}

jobject Minecraft::GetCurrentScreen(JNIEnv* env) {
    const std::string fieldName = Mapper::Get("currentScreen");
    const std::string fieldSignature = Mapper::Get("net/minecraft/client/gui/GuiScreen", 2);
    if (fieldName.empty() || fieldSignature.empty()) {
        return nullptr;
    }

    return GetMinecraftMember(env, fieldName.c_str(), fieldSignature.c_str());
}

jobject Minecraft::GetPlayerController(JNIEnv* env) {
    const std::string fieldName = Mapper::Get("playerController");
    const std::string fieldSignature = Mapper::Get("net/minecraft/client/multiplayer/PlayerControllerMP", 2);
    if (fieldName.empty() || fieldSignature.empty()) {
        return nullptr;
    }

    return GetMinecraftMember(env, fieldName.c_str(), fieldSignature.c_str());
}
