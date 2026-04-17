#include "pch.h"
#include "Scoreboard.h"

#include "Team.h"

#include "../jni/Class.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

jobject Scoreboard::GetPlayersTeam(JNIEnv* env, const std::string& playerName) {
    if (!env || !this || playerName.empty()) {
        return nullptr;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getPlayersTeam");
    const std::string teamSignature = Mapper::Get("net/minecraft/scoreboard/ScorePlayerTeam", 2);
    const std::string signature = "(Ljava/lang/String;)" + teamSignature;
    Method* method = (methodName.empty() || teamSignature.empty()) ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), signature.c_str());

    jstring playerNameObject = env->NewStringUTF(playerName.c_str());
    jobject team = (method && playerNameObject) ? method->CallObjectMethod(env, this, false, playerNameObject) : nullptr;
    if (playerNameObject) {
        env->DeleteLocalRef(playerNameObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
    return team;
}

jobject Scoreboard::GetObjectiveInDisplaySlot(JNIEnv* env, int slot) {
    if (!env || !this) {
        return nullptr;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getObjectiveInDisplaySlot");
    const std::string signature = "(I)" + Mapper::Get("net/minecraft/scoreboard/ScoreObjective", 2);
    Method* method = (methodName.empty() || signature.empty()) ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), signature.c_str());
    jobject value = method ? method->CallObjectMethod(env, this, false, slot) : nullptr;
    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
    return value;
}

jobject Scoreboard::GetValueFromObjective(JNIEnv* env, const std::string& playerName, jobject objective) {
    if (!env || !this || !objective || playerName.empty()) {
        return nullptr;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getValueFromObjective");
    const std::string objectiveSignature = Mapper::Get("net/minecraft/scoreboard/ScoreObjective", 2);
    const std::string scoreSignature = Mapper::Get("net/minecraft/scoreboard/Score", 2);
    const std::string signature = "(Ljava/lang/String;" + objectiveSignature + ")" + scoreSignature;
    Method* method = (methodName.empty() || objectiveSignature.empty() || scoreSignature.empty()) ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), signature.c_str());

    jstring playerNameObject = env->NewStringUTF(playerName.c_str());
    jobject value = (method && playerNameObject) ? method->CallObjectMethod(env, this, false, playerNameObject, objective) : nullptr;

    if (playerNameObject) {
        env->DeleteLocalRef(playerNameObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
    return value;
}

int Scoreboard::GetScorePoints(JNIEnv* env, jobject score) {
    if (!env || !score) {
        return -1;
    }

    auto* scoreClass = reinterpret_cast<Class*>(env->GetObjectClass(score));
    if (!scoreClass) {
        return -1;
    }

    const std::string methodName = Mapper::Get("getScorePoints");
    Method* method = methodName.empty() ? nullptr : scoreClass->GetMethod(env, methodName.c_str(), "()I");
    const int value = method ? method->CallIntMethod(env, score) : -1;
    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreClass));
    return value;
}
