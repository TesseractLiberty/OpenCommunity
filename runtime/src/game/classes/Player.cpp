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

#include <cmath>
#include <cctype>
#include <unordered_map>
#include <vector>

struct OriginalEntityData {
    AxisAlignedBB_t bb;
    float width;
    float height;
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

    Method* GetMappedMethod(JNIEnv* env, Class* ownerClass, const char* mappingKey, const char* signature) {
        if (!env || !ownerClass || !mappingKey || !signature || !signature[0]) {
            return nullptr;
        }

        const std::string methodName = Mapper::Get(mappingKey);
        return methodName.empty() ? nullptr : ownerClass->GetMethod(env, methodName.c_str(), signature);
    }

    Field* GetMappedField(JNIEnv* env, Class* ownerClass, const char* mappingKey, const char* signature) {
        if (!env || !ownerClass || !mappingKey || !signature || !signature[0]) {
            return nullptr;
        }

        const std::string fieldName = Mapper::Get(mappingKey);
        return fieldName.empty() ? nullptr : ownerClass->GetField(env, fieldName.c_str(), signature);
    }

    bool HasMinecraftFormatting(const std::string& text) {
        return text.find("\xC2\xA7") != std::string::npos ||
            text.find(static_cast<char>(0xA7)) != std::string::npos;
    }

    std::string TrimFormattedCandidate(std::string text) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
            text.erase(text.begin());
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
            text.pop_back();
        }
        return text;
    }

    std::string ExtractFormattedTagFromDisplayName(const std::string& formattedDisplayName, const std::string& plainName) {
        if (formattedDisplayName.empty() || plainName.empty()) {
            return {};
        }

        std::string clean;
        std::vector<size_t> cleanToOriginal;
        clean.reserve(formattedDisplayName.size());
        cleanToOriginal.reserve(formattedDisplayName.size());

        for (size_t index = 0; index < formattedDisplayName.size(); ++index) {
            const unsigned char current = static_cast<unsigned char>(formattedDisplayName[index]);
            if (current == 0xA7 || current == 0xC2) {
                if (current == 0xC2 && index + 1 < formattedDisplayName.size() &&
                    static_cast<unsigned char>(formattedDisplayName[index + 1]) == 0xA7) {
                    ++index;
                }
                if (index + 1 < formattedDisplayName.size()) {
                    ++index;
                }
                continue;
            }

            cleanToOriginal.push_back(index);
            clean.push_back(formattedDisplayName[index]);
        }

        const size_t namePos = clean.find(plainName);
        if (namePos == std::string::npos) {
            return {};
        }

        const size_t originalStart = namePos < cleanToOriginal.size() ? cleanToOriginal[namePos] : formattedDisplayName.size();
        const size_t originalEnd = (namePos + plainName.size()) < cleanToOriginal.size()
            ? cleanToOriginal[namePos + plainName.size()]
            : formattedDisplayName.size();

        std::string before = TrimFormattedCandidate(formattedDisplayName.substr(0, originalStart));
        std::string after = originalEnd < formattedDisplayName.size()
            ? TrimFormattedCandidate(formattedDisplayName.substr(originalEnd))
            : std::string{};

        if (CleanTagDelimiters(StripFormatting(before)).size() >= 2) {
            return before;
        }
        if (CleanTagDelimiters(StripFormatting(after)).size() >= 2) {
            return after;
        }

        return {};
    }

    std::string GetDisplayNameText(JNIEnv* env, jobject playerObject, bool formatted) {
        if (!env || !playerObject || !g_Game || !g_Game->IsInitialized()) {
            return {};
        }

        Class* playerClass = GetPlayerClass(env, playerObject);
        if (!playerClass) {
            return {};
        }

        const std::string chatSignature = Mapper::Get("net/minecraft/util/IChatComponent", 3);
        Method* displayNameMethod = chatSignature.empty() ? nullptr : GetMappedMethod(env, playerClass, "getDisplayName", chatSignature.c_str());
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

        Method* textMethod = GetMappedMethod(env, chatClass, formatted ? "getFormattedText" : "getUnformattedTextForChat", "()Ljava/lang/String;");
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

    Method* getNameMethod = GetMappedMethod(env, playerClass, "getName", "()Ljava/lang/String;");
    std::string result;

    if (getNameMethod) {
        jstring value = static_cast<jstring>(getNameMethod->CallObjectMethod(env, this));
        result = ReadJString(env, value);
        if (value) {
            env->DeleteLocalRef(value);
        }
    }

    if (result.empty()) {
        Method* getGameProfileMethod = GetMappedMethod(env, playerClass, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
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
        result = GetDisplayNameText(env, reinterpret_cast<jobject>(this), false);
    }

    return stripFormatting ? StripFormatting(result) : result;
}

std::string Player::GetFormattedDisplayName(JNIEnv* env) {
    return GetDisplayNameText(env, reinterpret_cast<jobject>(this), true);
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

    std::string displayName = StripFormatting(GetDisplayNameText(env, reinterpret_cast<jobject>(this), false));
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

    const std::string formattedDisplayName = GetFormattedDisplayName(env);
    const std::string displayTag = ExtractFormattedTagFromDisplayName(formattedDisplayName, name);

    const std::string suffix = team->GetColorSuffix(env);
    const std::string cleanSuffix = CleanTagDelimiters(StripFormatting(suffix));
    if (!cleanSuffix.empty()) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(team));
        return !HasMinecraftFormatting(suffix) && HasMinecraftFormatting(displayTag) ? displayTag : suffix;
    }

    const std::string prefix = team->GetColorPrefix(env);
    const std::string cleanPrefix = CleanTagDelimiters(StripFormatting(prefix));
    env->DeleteLocalRef(reinterpret_cast<jobject>(team));
    if (!cleanPrefix.empty()) {
        return !HasMinecraftFormatting(prefix) && HasMinecraftFormatting(displayTag) ? displayTag : prefix;
    }

    return displayTag;
}

float Player::GetHealth(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    Method* method = GetMappedMethod(env, playerClass, "getHealth", "()F");
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

    Method* method = GetMappedMethod(env, playerClass, "getMaxHealth", "()F");
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

    Field* field = GetMappedField(env, playerClass, "rotationPitch", "F");
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

    Field* field = GetMappedField(env, playerClass, "rotationYaw", "F");
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

    Field* field = GetMappedField(env, playerClass, "prevRotationPitch", "F");
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

    Field* field = GetMappedField(env, playerClass, "prevRotationYaw", "F");
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

    const std::string entitySignature = Mapper::Get("net/minecraft/entity/Entity", 2);
    Method* method = entitySignature.empty() ? nullptr : GetMappedMethod(env, entityClass, "getDistanceToEntity", ("(" + entitySignature + ")F").c_str());
    return method ? method->CallFloatMethod(env, this, false, entity) : 1000.0f;
}

float Player::GetEyeHeight(JNIEnv* env) {
    if (!env || !this) {
        return 1.62f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 1.62f;
    }

    Method* method = GetMappedMethod(env, playerClass, "getEyeHeight", "()F");
    const float value = method ? method->CallFloatMethod(env, this) : 1.62f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return std::isfinite(value) && value > 0.0f ? value : 1.62f;
}

float Player::GetWidth(JNIEnv* env) {
    if (!env || !this) {
        return 0.0f;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0.0f;
    }

    Field* field = GetMappedField(env, playerClass, "width", "F");
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

    Field* field = GetMappedField(env, playerClass, "height", "F");
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

    Field* field = GetMappedField(env, playerClass, "width", "F");
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

    Field* field = GetMappedField(env, playerClass, "height", "F");
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

    Method* method = GetMappedMethod(env, playerClass, "setPosition", "(DDD)V");
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

    Method* method = GetMappedMethod(env, playerClass, "setAlwaysRenderNameTag", "(Z)V");
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

    Method* method = GetMappedMethod(env, playerClass, "isInvisible", "()Z");
    const bool invisible = method ? method->CallBooleanMethod(env, this) : true;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return invisible;
}

bool Player::IsInWeb(JNIEnv* env) {
    if (!env || !this) {
        return false;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return false;
    }

    Field* field = GetMappedField(env, playerClass, "isInWeb", "Z");
    const bool inWeb = field ? field->GetBooleanField(env, this) : false;
    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
    return inWeb;
}

int Player::GetHurtTime(JNIEnv* env) {
    if (!env || !this) {
        return 0;
    }

    Class* playerClass = GetPlayerClass(env, reinterpret_cast<jobject>(this));
    if (!playerClass) {
        return 0;
    }

    Field* field = GetMappedField(env, playerClass, "hurtTime", "I");
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

    Field* field = GetMappedField(env, playerClass, "swingProgressInt", "I");
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

bool Player::HasZeroedBoundingBox(JNIEnv* env) {
    if (!env || this == nullptr) {
        return false;
    }

    float width = 0.0f;
    float height = 0.0f;
    jobject bbObject = nullptr;

    try {
        width = GetWidth(env);
        height = GetHeight(env);
        bbObject = GetBoundingBox(env);
    } catch (...) {
        if (bbObject) {
            env->DeleteLocalRef(bbObject);
        }
        return false;
    }

    if (width == 0.0f && height == 0.0f) {
        if (bbObject) {
            env->DeleteLocalRef(bbObject);
        }
        return true;
    }

    if (!bbObject) {
        return false;
    }

    const AxisAlignedBB_t boundingBox = reinterpret_cast<AxisAlignedBB*>(bbObject)->GetNativeBoundingBox(env);
    env->DeleteLocalRef(bbObject);

    return boundingBox.minX == 0.0f &&
        boundingBox.minY == 0.0f &&
        boundingBox.minZ == 0.0f &&
        boundingBox.maxX == 0.0f &&
        boundingBox.maxY == 0.0f &&
        boundingBox.maxZ == 0.0f;
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

            AxisAlignedBB_t bb{};
            bb.minX = 0.0; bb.minY = 0.0; bb.minZ = 0.0;
            bb.maxX = 0.0; bb.maxY = 0.0; bb.maxZ = 0.0;
            ((AxisAlignedBB*)bbObj)->SetNativeBoundingBox(bb, env);
            this->SetWidth(0.0f, env);
            this->SetHeight(0.0f, env);
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

    Field* jumpTicksField = GetMappedField(env, playerClass, "jumpTicks", "I");
    if (jumpTicksField) {
        jumpTicksField->SetIntField(env, this, ticks);
    }

    Method* setJumpingMethod = GetMappedMethod(env, playerClass, "setJumping", "(Z)V");
    if (setJumpingMethod) {
        setJumpingMethod->CallVoidMethod(env, this, false, false);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
}
