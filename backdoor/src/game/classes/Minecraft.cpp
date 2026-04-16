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

    jobject GetMinecraftMember(JNIEnv* env, const std::string& fieldName, const std::string& fieldSignature) {
        if (!env || fieldName.empty() || fieldSignature.empty()) {
            return nullptr;
        }

        Class* minecraftClass = GetMinecraftClass();
        if (!minecraftClass) {
            return nullptr;
        }

        Field* field = minecraftClass->GetField(env, fieldName.c_str(), fieldSignature.c_str());
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

    jobject GetGameSettings(JNIEnv* env) {
        return GetMinecraftMember(env, Mapper::Get("gameSettings"), Mapper::Get("net/minecraft/client/settings/GameSettings", 2));
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
    return field ? field->GetObjectField(env, minecraftClass, true) : nullptr;
}

jobject Minecraft::GetThePlayer(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("thePlayer"), Mapper::Get("net/minecraft/client/entity/EntityClientPlayerMP", 2));
}

jobject Minecraft::GetTheWorld(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("theWorld"), Mapper::Get("net/minecraft/client/multiplayer/WorldClient", 2));
}

jobject Minecraft::GetCurrentScreen(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("currentScreen"), Mapper::Get("net/minecraft/client/gui/GuiScreen", 2));
}

jobject Minecraft::GetPlayerController(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("playerController"), Mapper::Get("net/minecraft/client/multiplayer/PlayerControllerMP", 2));
}

jobject Minecraft::GetObjectMouseOver(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("objectMouseOver"), Mapper::Get("net/minecraft/util/MovingObjectPosition", 2));
}

jobject Minecraft::GetRenderItem(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("renderItem"), Mapper::Get("net/minecraft/client/renderer/entity/RenderItem", 2));
}

jobject Minecraft::GetKeyBindUseItem(JNIEnv* env) {
    if (!env) {
        return nullptr;
    }

    jobject gameSettings = GetGameSettings(env);
    if (!gameSettings) {
        return nullptr;
    }

    const std::string className = Mapper::Get("net/minecraft/client/settings/GameSettings");
    const std::string fieldName = Mapper::Get("keyBindUseItem");
    const std::string fieldSignature = Mapper::Get("net/minecraft/client/settings/KeyBinding", 2);
    if (className.empty() || fieldName.empty() || fieldSignature.empty()) {
        env->DeleteLocalRef(gameSettings);
        return nullptr;
    }

    Class* settingsClass = g_Game->FindClass(className);
    Field* field = settingsClass ? settingsClass->GetField(env, fieldName.c_str(), fieldSignature.c_str()) : nullptr;
    jobject value = field ? field->GetObjectField(env, gameSettings) : nullptr;
    env->DeleteLocalRef(gameSettings);
    return value;
}
