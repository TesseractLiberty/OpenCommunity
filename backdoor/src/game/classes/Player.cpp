#include "pch.h"
#include "Player.h"

#include "AxisAlignedBB.h"
#include "Minecraft.h"
#include "Scoreboard.h"
#include "Team.h"
#include "World.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

#include <cctype>
#include <unordered_map>

struct OriginalEntityData {
    AxisAlignedBB_t bb;
    float width;
    float height;
    double savedX{0};
    double savedY{0};
    double savedZ{0};
    bool hasPosition{false};
};
static std::unordered_map<std::string, OriginalEntityData> originalData;
static std::mutex originalDataMutex;

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

    std::string CleanTagDelimiters(const std::string& text) {
        if (text.empty()) {
            return {};
        }

        size_t start = 0;
        while (start < text.size() && static_cast<unsigned char>(text[start]) <= 32) {
            ++start;
        }
        while (start < text.size() && !std::isalnum(static_cast<unsigned char>(text[start]))) {
            ++start;
        }
        if (start >= text.size()) {
            return {};
        }

        size_t end = text.size() - 1;
        while (end > start && static_cast<unsigned char>(text[end]) <= 32) {
            --end;
        }
        while (end > start && !std::isalnum(static_cast<unsigned char>(text[end]))) {
            --end;
        }

        return text.substr(start, end - start + 1);
    }

    std::string ReadJString(JNIEnv* env, jstring stringObject) {
        if (!env || !stringObject) {
            return {};
        }

        const char* chars = env->GetStringUTFChars(stringObject, nullptr);
        std::string result = chars ? chars : "";
        if (chars) {
            env->ReleaseStringUTFChars(stringObject, chars);
        }
        return result;
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

    Method* FindMethod(JNIEnv* env, Class* ownerClass, const char* const* names, const char* signature) {
        if (!env || !ownerClass || !signature) {
            return nullptr;
        }

        for (int index = 0; names[index]; ++index) {
            const char* name = names[index];
            if (!name || !name[0]) {
                continue;
            }

            Method* method = ownerClass->GetMethod(env, name, signature);
            if (method) {
                return method;
            }
        }

        return nullptr;
    }

    Field* FindField(JNIEnv* env, Class* ownerClass, const char* const* names, const char* signature) {
        if (!env || !ownerClass || !signature) {
            return nullptr;
        }

        for (int index = 0; names[index]; ++index) {
            const char* name = names[index];
            if (!name || !name[0]) {
                continue;
            }

            Field* field = ownerClass->GetField(env, name, signature);
            if (field) {
                return field;
            }
        }

        return nullptr;
    }

    std::string GetDisplayNameText(JNIEnv* env, jobject playerObject) {
        if (!env || !playerObject || !g_Game || !g_Game->IsInitialized()) {
            return {};
        }

        Class* playerClass = GetPlayerClass(env, playerObject);
        if (!playerClass) {
            return {};
        }

        const std::string mappedDisplayName = Mapper::Get("getDisplayName");
        const char* displayNames[] = {
            mappedDisplayName.c_str(),
            "getDisplayName",
            "func_145748_c_",
            "f_",
            nullptr
        };
        const std::string chatSignature = Mapper::Get("net/minecraft/util/IChatComponent", 3);
        Method* displayNameMethod = chatSignature.empty() ? nullptr : FindMethod(env, playerClass, displayNames, chatSignature.c_str());
        if (!displayNameMethod) {
            env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
            return {};
        }

        jobject chatComponent = displayNameMethod->CallObjectMethod(env, playerObject);
        env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
        if (!chatComponent) {
            return {};
        }

        const std::string chatClassName = Mapper::Get("net/minecraft/util/IChatComponent");
        Class* chatClass = chatClassName.empty() ? nullptr : g_Game->FindClass(chatClassName);
        if (!chatClass) {
            env->DeleteLocalRef(chatComponent);
            return {};
        }

        const std::string mappedUnformattedText = Mapper::Get("getUnformattedTextForChat");
        const char* textNames[] = {
            mappedUnformattedText.c_str(),
            "getUnformattedTextForChat",
            "func_150261_e",
            "e",
            nullptr
        };
        Method* textMethod = FindMethod(env, chatClass, textNames, "()Ljava/lang/String;");
        if (!textMethod) {
            env->DeleteLocalRef(chatComponent);
            return {};
        }

        jstring textObject = static_cast<jstring>(textMethod->CallObjectMethod(env, chatComponent));
        std::string text = ReadJString(env, textObject);
        if (textObject) {
            env->DeleteLocalRef(textObject);
        }
        env->DeleteLocalRef(chatComponent);
        return text;
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

    const std::string mappedGetName = Mapper::Get("getName");
    const char* methodNames[] = {
        mappedGetName.c_str(),
        "getName",
        "func_70005_c_",
        "e_",
        nullptr
    };

    Method* getNameMethod = FindMethod(env, playerClass, methodNames, "()Ljava/lang/String;");
    std::string result;

    if (getNameMethod) {
        jstring value = static_cast<jstring>(getNameMethod->CallObjectMethod(env, this));
        result = ReadJString(env, value);
        if (value) {
            env->DeleteLocalRef(value);
        }
    }

    if (result.empty()) {
        const std::string mappedGetGameProfile = Mapper::Get("getGameProfile");
        const char* profileMethodNames[] = {
            mappedGetGameProfile.c_str(),
            "getGameProfile",
            "func_146103_bH",
            "cd",
            nullptr
        };

        Method* getGameProfileMethod = FindMethod(env, playerClass, profileMethodNames, "()Lcom/mojang/authlib/GameProfile;");
        if (getGameProfileMethod) {
            jobject profileObject = getGameProfileMethod->CallObjectMethod(env, reinterpret_cast<jobject>(this));
            if (profileObject) {
                jclass profileClass = env->GetObjectClass(profileObject);
                if (profileClass) {
                    jmethodID getProfileNameMethod = env->GetMethodID(profileClass, "getName", "()Ljava/lang/String;");
                    if (getProfileNameMethod) {
                        jstring profileNameObject = static_cast<jstring>(env->CallObjectMethod(profileObject, getProfileNameMethod));
                        result = ReadJString(env, profileNameObject);
                        if (profileNameObject) {
                            env->DeleteLocalRef(profileNameObject);
                        }
                    }
                    env->DeleteLocalRef(profileClass);
                }

                env->DeleteLocalRef(profileObject);
            }
        }
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));

    if (result.empty()) {
        result = GetDisplayNameText(env, reinterpret_cast<jobject>(this));
    }

    return stripFormatting ? StripFormatting(result) : result;
}

std::string Player::GetClanTag(JNIEnv* env, Scoreboard* scoreboard) {
    if (!env || !this) {
        return {};
    }

    const std::string name = GetName(env, true);
    if (name.empty()) {
        return {};
    }

    if (scoreboard) {
        auto* team = reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, name));
        if (team) {
            const std::string suffix = CleanTagDelimiters(StripFormatting(team->GetColorSuffix(env)));
            if (suffix.size() >= 2) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(team));
                return suffix;
            }

            const std::string prefix = CleanTagDelimiters(StripFormatting(team->GetColorPrefix(env)));
            env->DeleteLocalRef(reinterpret_cast<jobject>(team));
            if (prefix.size() >= 2) {
                return prefix;
            }
        }
    }

    std::string displayName = StripFormatting(GetDisplayNameText(env, reinterpret_cast<jobject>(this)));
    if (displayName.empty()) {
        return {};
    }

    const size_t playerPos = displayName.find(name);
    if (playerPos != std::string::npos) {
        displayName.erase(playerPos, name.size());
    }

    const std::string cleaned = CleanTagDelimiters(displayName);
    return cleaned.size() >= 2 ? cleaned : std::string{};
}

std::string Player::GetFormattedClanTag(JNIEnv* env, Scoreboard* scoreboard) {
    if (!env || !this || !scoreboard) {
        return {};
    }

    const std::string name = GetName(env, true);
    if (name.empty()) {
        return {};
    }

    auto* team = reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, name));
    if (!team) {
        return {};
    }

    const std::string suffix = team->GetColorSuffix(env);
    const std::string cleanSuffix = CleanTagDelimiters(StripFormatting(suffix));
    if (!cleanSuffix.empty()) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(team));
        return suffix;
    }

    const std::string prefix = team->GetColorPrefix(env);
    const std::string cleanPrefix = CleanTagDelimiters(StripFormatting(prefix));
    env->DeleteLocalRef(reinterpret_cast<jobject>(team));
    return cleanPrefix.empty() ? std::string{} : prefix;
}

float Player::GetHealth(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedGetHealth = Mapper::Get("getHealth");
    const char* methodNames[] = {
        mappedGetHealth.c_str(),
        "getHealth",
        "func_110143_aJ",
        "bn",
        nullptr
    };
    Method* method = FindMethod(env, playerClass, methodNames, "()F");
    const float value = method ? method->CallFloatMethod(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

float Player::GetMaxHealth(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedGetMaxHealth = Mapper::Get("getMaxHealth");
    const char* methodNames[] = {
        mappedGetMaxHealth.c_str(),
        "getMaxHealth",
        "func_110138_aP",
        "bu",
        nullptr
    };
    Method* method = FindMethod(env, playerClass, methodNames, "()F");
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

Vec3D Player::GetPos(JNIEnv* env) {
    if (!env || !this) {
        return {};
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return {};
    }

    Field* posXField = playerClass->GetField(env, Mapper::Get("posX").c_str(), "D");
    Field* posYField = playerClass->GetField(env, Mapper::Get("posY").c_str(), "D");
    Field* posZField = playerClass->GetField(env, Mapper::Get("posZ").c_str(), "D");
    Vec3D position;
    if (posXField && posYField && posZField) {
        position.x = posXField->GetDoubleField(env, this);
        position.y = posYField->GetDoubleField(env, this);
        position.z = posZField->GetDoubleField(env, this);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return position;
}

Vec3D Player::GetLastTickPos(JNIEnv* env) {
    if (!env || !this) {
        return {};
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return {};
    }

    Field* posXField = playerClass->GetField(env, Mapper::Get("lastTickPosX").c_str(), "D");
    Field* posYField = playerClass->GetField(env, Mapper::Get("lastTickPosY").c_str(), "D");
    Field* posZField = playerClass->GetField(env, Mapper::Get("lastTickPosZ").c_str(), "D");
    Vec3D position;
    if (posXField && posYField && posZField) {
        position.x = posXField->GetDoubleField(env, this);
        position.y = posYField->GetDoubleField(env, this);
        position.z = posZField->GetDoubleField(env, this);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return position;
}

float Player::GetRotationPitch(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedField = Mapper::Get("rotationPitch");
    const char* fieldNames[] = {
        mappedField.c_str(),
        "rotationPitch",
        "field_70125_A",
        "z",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    const float value = field ? field->GetFloatField(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

float Player::GetRotationYaw(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedField = Mapper::Get("rotationYaw");
    const char* fieldNames[] = {
        mappedField.c_str(),
        "rotationYaw",
        "field_70177_z",
        "y",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    const float value = field ? field->GetFloatField(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

float Player::GetPrevRotationPitch(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedField = Mapper::Get("prevRotationPitch");
    const char* fieldNames[] = {
        mappedField.c_str(),
        "prevRotationPitch",
        "field_70127_C",
        "B",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    const float value = field ? field->GetFloatField(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

float Player::GetPrevRotationYaw(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedField = Mapper::Get("prevRotationYaw");
    const char* fieldNames[] = {
        mappedField.c_str(),
        "prevRotationYaw",
        "field_70126_B",
        "A",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    const float value = field ? field->GetFloatField(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
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

float Player::GetDistanceToEntity(jobject entity, JNIEnv* env) {
    if (!env || !this || !entity) {
        return 1000.0f;
    }

    Class* entityClass = g_Game ? g_Game->FindClass(Mapper::Get("net/minecraft/entity/Entity")) : nullptr;
    if (!entityClass) {
        return 1000.0f;
    }

    const std::string mappedDistanceMethod = Mapper::Get("getDistanceToEntity");
    const char* methodNames[] = {
        mappedDistanceMethod.c_str(),
        "getDistanceToEntity",
        "func_70032_d",
        "g",
        nullptr
    };
    const std::string entitySignature = Mapper::Get("net/minecraft/entity/Entity", 2);
    Method* method = entitySignature.empty() ? nullptr : FindMethod(env, entityClass, methodNames, ("(" + entitySignature + ")F").c_str());
    return method ? method->CallFloatMethod(env, this, false, entity) : 1000.0f;
}

float Player::GetWidth(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedWidthField = Mapper::Get("width");
    const char* fieldNames[] = {
        mappedWidthField.c_str(),
        "width",
        "field_70130_N",
        "J",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    const float value = field ? field->GetFloatField(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

float Player::GetHeight(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    const std::string mappedHeightField = Mapper::Get("height");
    const char* fieldNames[] = {
        mappedHeightField.c_str(),
        "height",
        "field_70131_O",
        "K",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    const float value = field ? field->GetFloatField(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return value;
}

void Player::SetWidth(float value, JNIEnv* env) {
    if (!env || !this) {
        return;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return;
    }

    const std::string mappedWidthField = Mapper::Get("width");
    const char* fieldNames[] = {
        mappedWidthField.c_str(),
        "width",
        "field_70130_N",
        "J",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    if (field) {
        field->SetFloatField(env, this, value);
    }
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
}

void Player::SetHeight(float value, JNIEnv* env) {
    if (!env || !this) {
        return;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return;
    }

    const std::string mappedHeightField = Mapper::Get("height");
    const char* fieldNames[] = {
        mappedHeightField.c_str(),
        "height",
        "field_70131_O",
        "K",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "F");
    if (field) {
        field->SetFloatField(env, this, value);
    }
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
}

void Player::SetPosition(double x, double y, double z, JNIEnv* env) {
    if (!env || !this) {
        return;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return;
    }

    const std::string mappedSetPosition = Mapper::Get("setPosition");
    const char* methodNames[] = {
        mappedSetPosition.c_str(),
        "setPosition",
        "func_70107_b",
        "b",
        nullptr
    };
    Method* method = FindMethod(env, playerClass, methodNames, "(DDD)V");
    if (method) {
        method->CallVoidMethod(env, this, false, x, y, z);
    }
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
}

void Player::SetAlwaysRenderNameTag(bool value, JNIEnv* env) {
    if (!env || !this) {
        return;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return;
    }

    const std::string mappedMethod = Mapper::Get("setAlwaysRenderNameTag");
    const char* methodNames[] = {
        mappedMethod.c_str(),
        "setAlwaysRenderNameTag",
        "func_174805_g",
        "g",
        nullptr
    };

    Method* method = FindMethod(env, playerClass, methodNames, "(Z)V");
    if (method) {
        method->CallVoidMethod(env, this, false, value);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
}

bool Player::IsInvisible(JNIEnv* env) {
    if (!env || !this) {
        return true;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return true;
    }

    const std::string mappedIsInvisible = Mapper::Get("isInvisible");
    const char* methodNames[] = {
        mappedIsInvisible.c_str(),
        "isInvisible",
        "func_82150_aj",
        "ax",
        nullptr
    };
    Method* method = FindMethod(env, playerClass, methodNames, "()Z");
    const bool invisible = method ? method->CallBooleanMethod(env, this) : true;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return invisible;
}

int Player::GetHurtTime(JNIEnv* env) {
    if (!env || !this) {
        return 0;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0;
    }

    const std::string mappedHurtTime = Mapper::Get("hurtTime");
    const char* fieldNames[] = {
        mappedHurtTime.c_str(),
        "hurtTime",
        "field_70737_aN",
        "au",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "I");
    const int hurtTime = field ? field->GetIntField(env, this) : 0;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return hurtTime;
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

int Player::GetSwingProgressInt(JNIEnv* env) {
    if (!env || !this) {
        return 0;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0;
    }

    const std::string mappedSwingProgress = Mapper::Get("swingProgressInt");
    const char* fieldNames[] = {
        mappedSwingProgress.c_str(),
        "swingProgressInt",
        "field_110158_av",
        "as",
        nullptr
    };
    Field* field = FindField(env, playerClass, fieldNames, "I");
    const int swingProgress = field ? field->GetIntField(env, this) : 0;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return swingProgress;
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

jobject Player::GetBoundingBox(JNIEnv* env) {
    if (!env || !this) {
        return nullptr;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return nullptr;
    }

    const std::string fieldName = Mapper::Get("boundingBox");
    const std::string fieldSignature = Mapper::Get("net/minecraft/util/AxisAlignedBB", 2);
    if (fieldName.empty() || fieldSignature.empty()) {
        env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
        return nullptr;
    }

    Field* bbField = playerClass->GetField(env, fieldName.c_str(), fieldSignature.c_str());
    jobject result = bbField ? bbField->GetObjectField(env, this) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return result;
}

void Player::Zero(JNIEnv* env) {
    if (!env || this == nullptr) return;

    std::string name;
    try {
        name = this->GetName(env, true);
    } catch (...) {
        return;
    }

    if (name.empty()) return;

    std::lock_guard<std::mutex> lock(originalDataMutex);

    if (originalData.find(name) == originalData.end()) {
        jobject bbObj = nullptr;
        try {
            bbObj = this->GetBoundingBox(env);
        } catch (...) { return; }
        if (!bbObj) return;

        try {
            const AxisAlignedBB_t curHitbox = ((AxisAlignedBB*)bbObj)->GetNativeBoundingBox(env);
            const float curWidth = this->GetWidth(env);
            const float curHeight = this->GetHeight(env);

            auto& data = originalData[name];
            data.bb = curHitbox;
            data.width = curWidth;
            data.height = curHeight;

            auto pos = this->GetPos(env);
            data.savedX = pos.x;
            data.savedY = pos.y;
            data.savedZ = pos.z;
            data.hasPosition = true;

            AxisAlignedBB_t bb{};
            bb.minX = 0.0; bb.minY = 0.0; bb.minZ = 0.0;
            bb.maxX = 0.0; bb.maxY = 0.0; bb.maxZ = 0.0;
            ((AxisAlignedBB*)bbObj)->SetNativeBoundingBox(bb, env);
            this->SetWidth(0.0f, env);
            this->SetHeight(0.0f, env);

            this->SetPosition(pos.x, -60.0, pos.z, env);
        } catch (...) {}

        env->DeleteLocalRef(bbObj);
        return;
    }

    jobject bbObj2 = nullptr;
    try {
        bbObj2 = this->GetBoundingBox(env);
    } catch (...) { return; }
    if (!bbObj2) return;

    try {
        AxisAlignedBB_t bb{};
        bb.minX = 0.0; bb.minY = 0.0; bb.minZ = 0.0;
        bb.maxX = 0.0; bb.maxY = 0.0; bb.maxZ = 0.0;
        ((AxisAlignedBB*)bbObj2)->SetNativeBoundingBox(bb, env);
        this->SetWidth(0.0f, env);
        this->SetHeight(0.0f, env);

        auto it = originalData.find(name);
        if (it != originalData.end() && it->second.hasPosition) {
            Vec3D curPos = this->GetPos(env);
            if (curPos.y > -50.0) {
                this->SetPosition(it->second.savedX, -60.0, it->second.savedZ, env);
            }
        }
    } catch (...) {}

    env->DeleteLocalRef(bbObj2);
}

void Player::Restore(JNIEnv* env) {
    if (!env || this == nullptr) return;

    std::string name;
    try {
        name = this->GetName(env, true);
    } catch (...) {
        return;
    }

    if (name.empty()) return;

    std::lock_guard<std::mutex> lock(originalDataMutex);

    auto it = originalData.find(name);
    if (it != originalData.end()) {
        try {
            if (it->second.hasPosition) {
                this->SetPosition(it->second.savedX, it->second.savedY, it->second.savedZ, env);
            }
            this->SetWidth(it->second.width, env);
            this->SetHeight(it->second.height, env);
            jobject bbObj = this->GetBoundingBox(env);
            if (bbObj) {
                ((AxisAlignedBB*)bbObj)->SetNativeBoundingBox(it->second.bb, env);
                env->DeleteLocalRef(bbObj);
            }
        } catch (...) {}
        originalData.erase(it);
    }
}

void Player::SendPacket(jobject packet, JNIEnv* env) {
    if (!env || !this || !packet) {
        return;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return;
    }

    const std::string sendQueueFieldName = Mapper::Get("sendQueue");
    const std::string sendQueueSignature = Mapper::Get("net/minecraft/client/network/NetHandlerPlayClient", 2);
    const std::string packetSignature = Mapper::Get("net/minecraft/network/Packet", 2);
    const std::string addToSendQueueName = Mapper::Get("addToSendQueue");
    if (sendQueueFieldName.empty() || sendQueueSignature.empty() || packetSignature.empty() || addToSendQueueName.empty()) {
        env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
        return;
    }

    Field* sendQueueField = playerClass->GetField(env, sendQueueFieldName.c_str(), sendQueueSignature.c_str());
    jobject sendQueue = sendQueueField ? sendQueueField->GetObjectField(env, this) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    if (!sendQueue) {
        return;
    }

    auto* sendQueueClass = reinterpret_cast<Class*>(env->GetObjectClass(sendQueue));
    if (!sendQueueClass) {
        env->DeleteLocalRef(sendQueue);
        return;
    }

    Method* addToSendQueueMethod = sendQueueClass->GetMethod(env, addToSendQueueName.c_str(), ("(" + packetSignature + ")V").c_str());
    if (addToSendQueueMethod) {
        addToSendQueueMethod->CallVoidMethod(env, sendQueue, false, packet);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(sendQueueClass));
    env->DeleteLocalRef(sendQueue);
}

void Player::SetJumpTicks(int ticks, JNIEnv* env) {
    if (!env || !this) {
        return;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return;
    }

    const std::string mappedJumpTicks = Mapper::Get("jumpTicks");
    const char* fieldNames[] = {
        mappedJumpTicks.c_str(),
        "bn",
        "field_70773_bE",
        nullptr
    };

    Field* jumpTicksField = FindField(env, playerClass, fieldNames, "I");
    if (jumpTicksField) {
        jumpTicksField->SetIntField(env, this, ticks);
    }

    const std::string mappedSetJumping = Mapper::Get("setJumping");
    const char* methodNames[] = {
        mappedSetJumping.c_str(),
        "setJumping",
        nullptr
    };

    Method* setJumpingMethod = FindMethod(env, playerClass, methodNames, "(Z)V");
    if (setJumpingMethod) {
        setJumpingMethod->CallVoidMethod(env, this, false, false);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
}
