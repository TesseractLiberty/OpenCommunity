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
    if (!controllerClass) {
        env->DeleteLocalRef(controller);
        return;
    }

    const std::string signature = "(IIII" + playerSignature + ")" + stackSignature;
    Method* method = controllerClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject result = method ? method->CallObjectMethod(env, controller, false, windowId, slotId, mouseButtonClicked, mode, player) : nullptr;
    if (result) {
        env->DeleteLocalRef(result);
    }

    env->DeleteLocalRef(controller);
}
