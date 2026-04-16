#include "pch.h"
#include "PlayerController.h"

#include "Minecraft.h"
#include "../jni/Class.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

void PlayerController::WindowClick(jobject player, int windowId, int slotId, int mouseButtonClicked, int mode, JNIEnv* env) {
    if (!env || !player || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    jobject controller = Minecraft::GetPlayerController(env);
    if (!controller) {
        return;
    }

    const std::string className = Mapper::Get("net/minecraft/client/multiplayer/PlayerControllerMP");
    const std::string methodName = Mapper::Get("windowClick");
    const std::string playerSignature = Mapper::Get("net/minecraft/entity/player/EntityPlayer", 2);
    const std::string stackSignature = Mapper::Get("net/minecraft/item/ItemStack", 2);
    if (className.empty() || methodName.empty() || playerSignature.empty() || stackSignature.empty()) {
        env->DeleteLocalRef(controller);
        return;
    }

    Class* controllerClass = g_Game->FindClass(className);
    const std::string signature = "(IIII" + playerSignature + ")" + stackSignature;
    Method* method = controllerClass ? controllerClass->GetMethod(env, methodName.c_str(), signature.c_str()) : nullptr;
    jobject result = method ? method->CallObjectMethod(env, controller, false, windowId, slotId, mouseButtonClicked, mode, player) : nullptr;
    if (result) {
        env->DeleteLocalRef(result);
    }

    env->DeleteLocalRef(controller);
}

bool PlayerController::SendUseItem(jobject player, jobject world, jobject stack, JNIEnv* env) {
    if (!env || !player || !world || !stack || !g_Game || !g_Game->IsInitialized()) {
        return false;
    }

    jobject controller = Minecraft::GetPlayerController(env);
    if (!controller) {
        return false;
    }

    const std::string className = Mapper::Get("net/minecraft/client/multiplayer/PlayerControllerMP");
    const std::string methodName = Mapper::Get("sendUseItem");
    const std::string playerSignature = Mapper::Get("net/minecraft/entity/player/EntityPlayer", 2);
    const std::string worldSignature = Mapper::Get("net/minecraft/world/World", 2);
    const std::string stackSignature = Mapper::Get("net/minecraft/item/ItemStack", 2);
    if (className.empty() || methodName.empty() || playerSignature.empty() || worldSignature.empty() || stackSignature.empty()) {
        env->DeleteLocalRef(controller);
        return false;
    }

    const std::string signature = "(" + playerSignature + worldSignature + stackSignature + ")Z";
    Class* controllerClass = g_Game->FindClass(className);
    Method* method = controllerClass ? controllerClass->GetMethod(env, methodName.c_str(), signature.c_str()) : nullptr;
    const bool result = method ? method->CallBooleanMethod(env, controller, false, player, world, stack) : false;
    env->DeleteLocalRef(controller);
    return result;
}

void PlayerController::UpdateController(JNIEnv* env) {
    if (!env || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    jobject controller = Minecraft::GetPlayerController(env);
    if (!controller) {
        return;
    }

    const std::string className = Mapper::Get("net/minecraft/client/multiplayer/PlayerControllerMP");
    const std::string methodName = Mapper::Get("updateController");
    if (className.empty() || methodName.empty()) {
        env->DeleteLocalRef(controller);
        return;
    }

    Class* controllerClass = g_Game->FindClass(className);
    Method* method = controllerClass ? controllerClass->GetMethod(env, methodName.c_str(), "()V") : nullptr;
    if (method) {
        method->CallVoidMethod(env, controller);
    }

    env->DeleteLocalRef(controller);
}
