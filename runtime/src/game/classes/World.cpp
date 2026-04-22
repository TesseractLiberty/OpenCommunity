#include "pch.h"
#include "World.h"

#include "Player.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/Method.h"
#include "../jni/GameInstance.h"
#include "../mapping/Mapper.h"

std::vector<Player*> World::GetPlayerEntities(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return {};
    }

    auto* worldClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!worldClass) {
        return {};
    }

    Field* playerEntitiesField = worldClass->GetField(env, Mapper::Get("playerEntities").c_str(), "Ljava/util/List;");
    env->DeleteLocalRef(reinterpret_cast<jclass>(worldClass));
    if (!playerEntitiesField) {
        return {};
    }

    jobject listObject = playerEntitiesField->GetObjectField(env, this);
    if (!listObject) {
        return {};
    }

    Class* listClass = g_Game->FindClass("java/util/List");
    Method* toArrayMethod = listClass ? listClass->GetMethod(env, "toArray", "()[Ljava/lang/Object;") : nullptr;
    jobjectArray playerArray = toArrayMethod ? static_cast<jobjectArray>(toArrayMethod->CallObjectMethod(env, listObject)) : nullptr;
    env->DeleteLocalRef(listObject);
    if (!playerArray) {
        return {};
    }

    const jsize length = env->GetArrayLength(playerArray);
    std::vector<Player*> players;
    players.reserve(length > 0 ? static_cast<size_t>(length) : 0);

    for (jsize index = 0; index < length; ++index) {
        jobject playerObject = env->GetObjectArrayElement(playerArray, index);
        if (playerObject) {
            players.push_back(reinterpret_cast<Player*>(playerObject));
        }
    }

    env->DeleteLocalRef(playerArray);
    return players;
}

std::vector<jobject> World::GetLoadedEntities(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return {};
    }

    auto* worldClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!worldClass) {
        return {};
    }

    Field* loadedEntitiesField = worldClass->GetField(env, Mapper::Get("loadedEntityList").c_str(), "Ljava/util/List;");
    env->DeleteLocalRef(reinterpret_cast<jclass>(worldClass));
    if (!loadedEntitiesField) {
        return {};
    }

    jobject listObject = loadedEntitiesField->GetObjectField(env, this);
    if (!listObject) {
        return {};
    }

    Class* listClass = g_Game->FindClass("java/util/List");
    Method* toArrayMethod = listClass ? listClass->GetMethod(env, "toArray", "()[Ljava/lang/Object;") : nullptr;
    jobjectArray entityArray = toArrayMethod ? static_cast<jobjectArray>(toArrayMethod->CallObjectMethod(env, listObject)) : nullptr;
    env->DeleteLocalRef(listObject);
    if (!entityArray) {
        return {};
    }

    const jsize length = env->GetArrayLength(entityArray);
    std::vector<jobject> entities;
    entities.reserve(length > 0 ? static_cast<size_t>(length) : 0);

    for (jsize index = 0; index < length; ++index) {
        jobject entityObject = env->GetObjectArrayElement(entityArray, index);
        if (entityObject) {
            entities.push_back(entityObject);
        }
    }

    env->DeleteLocalRef(entityArray);
    return entities;
}

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
