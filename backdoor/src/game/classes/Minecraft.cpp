#include "pch.h"
#include "Minecraft.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
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

void Minecraft::SetObjectMouseOver(jobject movingObject, JNIEnv* env) {
    if (!env) {
        return;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return;
    }

    const std::string fieldName = Mapper::Get("objectMouseOver");
    const std::string fieldSignature = Mapper::Get("net/minecraft/util/MovingObjectPosition", 2);
    if (fieldName.empty() || fieldSignature.empty()) {
        return;
    }

    Field* field = minecraftClass->GetField(env, fieldName.c_str(), fieldSignature.c_str());
    if (!field) {
        return;
    }

    jobject minecraft = GetTheMinecraft(env);
    if (!minecraft) {
        return;
    }

    field->SetObjectField(env, minecraft, movingObject);
    env->DeleteLocalRef(minecraft);
}

jobject Minecraft::GetRenderItem(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("renderItem"), Mapper::Get("net/minecraft/client/renderer/entity/RenderItem", 2));
}

jobject Minecraft::GetEntityRenderer(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("entityRenderer"), Mapper::Get("net/minecraft/client/renderer/EntityRenderer", 2));
}

jobject Minecraft::GetRenderViewEntity(JNIEnv* env) {
    if (!env) {
        return nullptr;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getRenderViewEntity");
    const std::string methodSignature = Mapper::Get("net/minecraft/entity/Entity", 3);
    if (!methodName.empty() && !methodSignature.empty()) {
        Method* method = minecraftClass->GetMethod(env, methodName.c_str(), methodSignature.c_str());
        if (method) {
            jobject minecraft = GetTheMinecraft(env);
            if (minecraft) {
                jobject value = method->CallObjectMethod(env, minecraft);
                env->DeleteLocalRef(minecraft);
                if (value) {
                    return value;
                }
            }
        }
    }

    jobject value = GetMinecraftMember(env, Mapper::Get("renderViewEntity"), Mapper::Get("net/minecraft/entity/Entity", 2));
    return value ? value : GetThePlayer(env);
}

jobject Minecraft::GetRenderManager(JNIEnv* env) {
    if (!env) {
        return nullptr;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getRenderManager");
    const std::string methodSignature = Mapper::Get("net/minecraft/client/renderer/entity/RenderManager", 3);
    if (methodName.empty() || methodSignature.empty()) {
        return nullptr;
    }

    Method* method = minecraftClass->GetMethod(env, methodName.c_str(), methodSignature.c_str());
    if (!method) {
        return nullptr;
    }

    jobject minecraft = GetTheMinecraft(env);
    if (!minecraft) {
        return nullptr;
    }

    jobject value = method->CallObjectMethod(env, minecraft);
    env->DeleteLocalRef(minecraft);
    return value;
}

jobject Minecraft::GetNetHandler(JNIEnv* env) {
    if (!env) {
        return nullptr;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getNetHandler");
    const std::string methodSignature = "()" + Mapper::Get("net/minecraft/client/network/NetHandlerPlayClient", 2);
    if (methodName.empty() || methodSignature.empty()) {
        return nullptr;
    }

    Method* method = minecraftClass->GetMethod(env, methodName.c_str(), methodSignature.c_str());
    if (!method) {
        return nullptr;
    }

    jobject minecraft = GetTheMinecraft(env);
    if (!minecraft) {
        return nullptr;
    }

    jobject value = method->CallObjectMethod(env, minecraft);
    env->DeleteLocalRef(minecraft);
    return value;
}

jobject Minecraft::GetTimer(JNIEnv* env) {
    return GetMinecraftMember(env, Mapper::Get("timer"), Mapper::Get("net/minecraft/util/Timer", 2));
}

int Minecraft::GetThirdPersonView(JNIEnv* env) {
    if (!env) {
        return 0;
    }

    jobject gameSettings = GetGameSettings(env);
    if (!gameSettings) {
        return 0;
    }

    const std::string className = Mapper::Get("net/minecraft/client/settings/GameSettings");
    const std::string fieldName = Mapper::Get("thirdPersonView");
    if (className.empty() || fieldName.empty()) {
        env->DeleteLocalRef(gameSettings);
        return 0;
    }

    Class* settingsClass = g_Game->FindClass(className);
    Field* field = settingsClass ? settingsClass->GetField(env, fieldName.c_str(), "I") : nullptr;
    const int value = field ? field->GetIntField(env, gameSettings) : 0;
    env->DeleteLocalRef(gameSettings);
    return value;
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

void Minecraft::DisplayGuiScreen(jobject guiScreen, JNIEnv* env) {
    if (!env) {
        return;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return;
    }

    const std::string screenSignature = Mapper::Get("net/minecraft/client/gui/GuiScreen", 2);
    const std::string methodName = Mapper::Get("displayGuiScreen");
    if (screenSignature.empty() || methodName.empty()) {
        return;
    }

    Method* method = minecraftClass->GetMethod(env, methodName.c_str(), ("(" + screenSignature + ")V").c_str());
    if (!method) {
        return;
    }

    jobject minecraft = GetTheMinecraft(env);
    if (!minecraft) {
        return;
    }

    method->CallVoidMethod(env, minecraft, false, guiScreen);
    env->DeleteLocalRef(minecraft);
}

jobject Minecraft::CreateGuiInventory(jobject player, JNIEnv* env) {
    if (!env || !player || !g_Game || !g_Game->IsInitialized()) {
        return nullptr;
    }

    const std::string className = Mapper::Get("net/minecraft/client/gui/inventory/GuiInventory");
    const std::string playerSignature = Mapper::Get("net/minecraft/entity/player/EntityPlayer", 2);
    if (className.empty() || playerSignature.empty()) {
        return nullptr;
    }

    jclass inventoryClass = reinterpret_cast<jclass>(g_Game->FindClass(className));
    if (!inventoryClass) {
        return nullptr;
    }

    jmethodID constructor = env->GetMethodID(inventoryClass, "<init>", ("(" + playerSignature + ")V").c_str());
    return constructor ? env->NewObject(inventoryClass, constructor, player) : nullptr;
}

void Minecraft::SetLeftClickCounter(int value, JNIEnv* env) {
    if (!env) {
        return;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return;
    }

    const std::string fieldName = Mapper::Get("leftClickCounter");
    Field* field = fieldName.empty() ? nullptr : minecraftClass->GetField(env, fieldName.c_str(), "I");
    if (!field) {
        return;
    }

    jobject minecraft = GetTheMinecraft(env);
    if (!minecraft) {
        return;
    }

    field->SetIntField(env, minecraft, value);
    env->DeleteLocalRef(minecraft);
}

void Minecraft::SetRightClickCounter(int value, JNIEnv* env) {
    if (!env) {
        return;
    }

    Class* minecraftClass = GetMinecraftClass();
    if (!minecraftClass) {
        return;
    }

    const std::string fieldName = Mapper::Get("rightClickDelayTimer");
    Field* field = fieldName.empty() ? nullptr : minecraftClass->GetField(env, fieldName.c_str(), "I");
    if (!field) {
        return;
    }

    jobject minecraft = GetTheMinecraft(env);
    if (!minecraft) {
        return;
    }

    field->SetIntField(env, minecraft, value);
    env->DeleteLocalRef(minecraft);
}
