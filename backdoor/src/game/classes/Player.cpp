#include "pch.h"
#include "Player.h"

#include "Minecraft.h"
#include "Scoreboard.h"
#include "World.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

namespace {
    std::string StripFormatting(std::string text) {
        std::string clean;
        clean.reserve(text.size());

        for (size_t index = 0; index < text.size(); ++index) {
            const unsigned char current = static_cast<unsigned char>(text[index]);
            if (current == 0xA7 || current == 0xC2) {
                if (current == 0xC2 && index + 1 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xA7) {
                    ++index;
                }
                if (index + 1 < text.size()) {
                    ++index;
                }
                continue;
            }

            clean.push_back(text[index]);
        }

        return clean;
    }

    Class* GetPlayerClass(JNIEnv* env, jobject playerObject) {
        return env && playerObject ? reinterpret_cast<Class*>(env->GetObjectClass(playerObject)) : nullptr;
    }

    jobject GetPlayerField(JNIEnv* env, jobject playerObject, const std::string& fieldName, const std::string& fieldSignature) {
        Class* playerClass = GetPlayerClass(env, playerObject);
        if (!playerClass || fieldName.empty() || fieldSignature.empty()) {
            return nullptr;
        }

        Field* field = playerClass->GetField(env, fieldName.c_str(), fieldSignature.c_str());
        jobject value = field ? field->GetObjectField(env, playerObject) : nullptr;
        env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
        return value;
    }
}

std::string Player::GetName(JNIEnv* env, bool stripFormatting) {
    if (!env || !this) {
        return {};
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return {};
    }

    const std::string methodName = Mapper::Get("getName");
    Method* getNameMethod = methodName.empty() ? nullptr : playerClass->GetMethod(env, methodName.c_str(), "()Ljava/lang/String;");
    std::string result;

    if (getNameMethod) {
        jstring value = static_cast<jstring>(getNameMethod->CallObjectMethod(env, this));
        if (value) {
            const char* chars = env->GetStringUTFChars(value, nullptr);
            if (chars) {
                result = chars;
                env->ReleaseStringUTFChars(value, chars);
            }
            env->DeleteLocalRef(value);
        }
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return stripFormatting ? StripFormatting(result) : result;
}

float Player::GetMaxHealth(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string methodName = Mapper::Get("getMaxHealth");
    Method* method = methodName.empty() ? nullptr : playerClass->GetMethod(env, methodName.c_str(), "()F");
    const float value = method ? method->CallFloatMethod(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

float Player::GetRealHealth(JNIEnv* env) {
    if (!env || !this) {
        return -1.0f;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        return -1.0f;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    jobject scoreboardObject = world->GetScoreboard(env);
    if (!scoreboardObject) {
        env->DeleteLocalRef(worldObject);
        return -1.0f;
    }

    auto* scoreboard = reinterpret_cast<Scoreboard*>(scoreboardObject);
    jobject objectiveObject = scoreboard->GetObjectiveInDisplaySlot(env, 2);
    if (!objectiveObject) {
        objectiveObject = scoreboard->GetObjectiveInDisplaySlot(env, 1);
    }

    if (!objectiveObject) {
        env->DeleteLocalRef(scoreboardObject);
        env->DeleteLocalRef(worldObject);
        return -1.0f;
    }

    const std::string playerName = GetName(env, true);
    jobject scoreObject = playerName.empty() ? nullptr : scoreboard->GetValueFromObjective(env, playerName, objectiveObject);
    if (!scoreObject) {
        env->DeleteLocalRef(objectiveObject);
        env->DeleteLocalRef(scoreboardObject);
        env->DeleteLocalRef(worldObject);
        return -1.0f;
    }

    const int scorePoints = Scoreboard::GetScorePoints(env, scoreObject);
    env->DeleteLocalRef(scoreObject);
    env->DeleteLocalRef(objectiveObject);
    env->DeleteLocalRef(scoreboardObject);
    env->DeleteLocalRef(worldObject);
    return scorePoints >= 0 ? static_cast<float>(scorePoints) : -1.0f;
}

jobject Player::GetHeldItem(JNIEnv* env) {
    if (!env || !this) {
        return nullptr;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getHeldItem");
    const std::string signature = Mapper::Get("net/minecraft/item/ItemStack", 3);
    Method* method = (methodName.empty() || signature.empty()) ? nullptr : playerClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject value = method ? method->CallObjectMethod(env, this) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

jobject Player::GetInventoryPlayer(JNIEnv* env) {
    return GetPlayerField(env, reinterpret_cast<jobject>(this), Mapper::Get("inventory"), Mapper::Get("net/minecraft/entity/player/InventoryPlayer", 2));
}

jobject Player::GetInventoryContainer(JNIEnv* env) {
    return GetPlayerField(env, reinterpret_cast<jobject>(this), Mapper::Get("inventoryContainer"), Mapper::Get("net/minecraft/inventory/Container", 2));
}

jobject Player::GetCurrentArmor(int slot, JNIEnv* env) {
    if (!env || !this) {
        return nullptr;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getCurrentArmor");
    const std::string signature = "(I)" + Mapper::Get("net/minecraft/item/ItemStack", 2);
    Method* method = (methodName.empty() || signature.empty()) ? nullptr : playerClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject value = method ? method->CallObjectMethod(env, this, false, slot) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

bool Player::IsUsingItem(JNIEnv* env) {
    if (!env || !this) {
        return false;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return false;
    }

    const std::string name = Mapper::Get("isUsingItem");
    Method* method = name.empty() ? nullptr : playerClass->GetMethod(env, name.c_str(), "()Z");
    bool value = false;

    if (method) {
        value = method->CallBooleanMethod(env, this);
    } else {
        Field* field = name.empty() ? nullptr : playerClass->GetField(env, name.c_str(), "Z");
        value = field ? field->GetBooleanField(env, this) : false;
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

jobject Player::GetActivePotionEffect(int potionId, JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return nullptr;
    }

    const std::string livingClassName = Mapper::Get("net/minecraft/entity/EntityLivingBase");
    const std::string potionClassName = Mapper::Get("net/minecraft/potion/Potion");
    const std::string potionSignature = Mapper::Get("net/minecraft/potion/Potion", 2);
    const std::string effectSignature = Mapper::Get("net/minecraft/potion/PotionEffect", 2);
    const std::string methodName = Mapper::Get("getActivePotionEffect");
    if (livingClassName.empty() || potionClassName.empty() || potionSignature.empty() || effectSignature.empty() || methodName.empty()) {
        return nullptr;
    }

    Class* livingClass = g_Game->FindClass(livingClassName);
    Class* potionClass = g_Game->FindClass(potionClassName);
    if (!livingClass || !potionClass) {
        return nullptr;
    }

    jobject potionObject = nullptr;
    const std::string regenerationFieldName = Mapper::Get("regeneration");
    if (potionId == 10 && !regenerationFieldName.empty()) {
        Field* regenerationField = potionClass->GetField(env, regenerationFieldName.c_str(), potionSignature.c_str(), true);
        potionObject = regenerationField ? regenerationField->GetObjectField(env, potionClass, true) : nullptr;
    }

    if (!potionObject) {
        const std::string potionTypesFieldName = Mapper::Get("potionTypes");
        if (!potionTypesFieldName.empty()) {
            const std::string arraySignature = "[" + potionSignature;
            Field* potionTypesField = potionClass->GetField(env, potionTypesFieldName.c_str(), arraySignature.c_str(), true);
            jobjectArray potionTypes = potionTypesField ? static_cast<jobjectArray>(potionTypesField->GetObjectField(env, potionClass, true)) : nullptr;
            if (potionTypes) {
                const jsize count = env->GetArrayLength(potionTypes);
                if (potionId >= 0 && potionId < count) {
                    potionObject = env->GetObjectArrayElement(potionTypes, potionId);
                }
                env->DeleteLocalRef(potionTypes);
            }
        }
    }

    if (!potionObject) {
        return nullptr;
    }

    const std::string signature = "(" + potionSignature + ")" + effectSignature;
    Method* method = livingClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject effect = method ? method->CallObjectMethod(env, this, false, potionObject) : nullptr;
    env->DeleteLocalRef(potionObject);
    return effect;
}
