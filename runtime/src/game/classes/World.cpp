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

bool World::IsWebBlockAt(JNIEnv* env, int x, int y, int z) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return false;
    }

    const std::string blockPosClassName = Mapper::Get("net/minecraft/util/BlockPos");
    const std::string blockPosSignature = Mapper::Get("net/minecraft/util/BlockPos", 2);
    const std::string blockStateSignature = Mapper::Get("net/minecraft/block/state/IBlockState", 2);
    const std::string blockSignature = Mapper::Get("net/minecraft/block/Block", 2);
    if (blockPosClassName.empty() || blockPosSignature.empty() ||
        blockStateSignature.empty() || blockSignature.empty()) {
        return false;
    }

    Class* blockPosClass = g_Game->FindClass(blockPosClassName);
    if (!blockPosClass) {
        return false;
    }

    jmethodID blockPosCtor = env->GetMethodID(reinterpret_cast<jclass>(blockPosClass), "<init>", "(III)V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    if (!blockPosCtor) {
        return false;
    }

    jobject blockPos = env->NewObject(
        reinterpret_cast<jclass>(blockPosClass),
        blockPosCtor,
        static_cast<jint>(x),
        static_cast<jint>(y),
        static_cast<jint>(z));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (blockPos) {
            env->DeleteLocalRef(blockPos);
        }
        return false;
    }
    if (!blockPos) {
        return false;
    }

    auto* worldClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!worldClass) {
        env->DeleteLocalRef(blockPos);
        return false;
    }

    const std::string getBlockStateName = Mapper::Get("getBlockState");
    const std::string getBlockStateSignature = "(" + blockPosSignature + ")" + blockStateSignature;
    Method* getBlockStateMethod = getBlockStateName.empty()
        ? nullptr
        : worldClass->GetMethod(env, getBlockStateName.c_str(), getBlockStateSignature.c_str());
    jobject blockState = getBlockStateMethod ? getBlockStateMethod->CallObjectMethod(env, this, false, blockPos) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(worldClass));
    env->DeleteLocalRef(blockPos);
    if (!blockState) {
        return false;
    }

    auto* blockStateClass = reinterpret_cast<Class*>(env->GetObjectClass(blockState));
    if (!blockStateClass) {
        env->DeleteLocalRef(blockState);
        return false;
    }

    const std::string getBlockName = Mapper::Get("getBlock");
    const std::string getBlockSignature = "()" + blockSignature;
    Method* getBlockMethod = getBlockName.empty()
        ? nullptr
        : blockStateClass->GetMethod(env, getBlockName.c_str(), getBlockSignature.c_str());
    jobject block = getBlockMethod ? getBlockMethod->CallObjectMethod(env, blockState) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(blockStateClass));
    env->DeleteLocalRef(blockState);
    if (!block) {
        return false;
    }

    Class* blocksClass = g_Game->FindClass(Mapper::Get("net/minecraft/init/Blocks"));
    Field* webField = blocksClass
        ? blocksClass->GetField(env, Mapper::Get("webBlock").c_str(), blockSignature.c_str(), true)
        : nullptr;
    jobject webBlock = webField ? webField->GetObjectField(env, blocksClass, true) : nullptr;
    const bool result = webBlock && env->IsSameObject(block, webBlock);

    if (webBlock) {
        env->DeleteLocalRef(webBlock);
    }
    env->DeleteLocalRef(block);
    return result;
}
