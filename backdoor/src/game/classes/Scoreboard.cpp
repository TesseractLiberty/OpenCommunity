#include "pch.h"
#include "Scoreboard.h"

#include "Team.h"

#include "../jni/Class.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

jobject Scoreboard::GetTeam(JNIEnv* env, const std::string& teamName) {
    if (!env || !this || teamName.empty()) {
        return nullptr;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("getTeam");
    const std::string teamSignature = Mapper::Get("net/minecraft/scoreboard/ScorePlayerTeam", 2);
    const std::string signature = "(Ljava/lang/String;)" + teamSignature;
    Method* method = (methodName.empty() || teamSignature.empty()) ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), signature.c_str());

    jstring teamNameObject = env->NewStringUTF(teamName.c_str());
    jobject team = (method && teamNameObject) ? method->CallObjectMethod(env, this, false, teamNameObject) : nullptr;
    if (teamNameObject) {
        env->DeleteLocalRef(teamNameObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
    return team;
}

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

jobject Scoreboard::CreateTeam(JNIEnv* env, const std::string& teamName) {
    if (!env || !this || teamName.empty()) {
        return nullptr;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return nullptr;
    }

    const std::string methodName = Mapper::Get("createTeam");
    const std::string teamSignature = Mapper::Get("net/minecraft/scoreboard/ScorePlayerTeam", 2);
    const std::string signature = "(Ljava/lang/String;)" + teamSignature;
    Method* method = (methodName.empty() || teamSignature.empty()) ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), signature.c_str());

    jstring teamNameObject = env->NewStringUTF(teamName.c_str());
    jobject team = (method && teamNameObject) ? method->CallObjectMethod(env, this, false, teamNameObject) : nullptr;
    if (teamNameObject) {
        env->DeleteLocalRef(teamNameObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
    return team;
}

bool Scoreboard::AddPlayerToTeam(JNIEnv* env, const std::string& playerName, const std::string& teamName) {
    if (!env || !this || playerName.empty() || teamName.empty()) {
        return false;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return false;
    }

    const std::string methodName = Mapper::Get("addPlayerToTeam");
    Method* method = methodName.empty() ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), "(Ljava/lang/String;Ljava/lang/String;)Z");

    jstring playerNameObject = env->NewStringUTF(playerName.c_str());
    jstring teamNameObject = env->NewStringUTF(teamName.c_str());
    const bool added = (method && playerNameObject && teamNameObject)
        ? method->CallBooleanMethod(env, this, false, playerNameObject, teamNameObject)
        : false;

    if (playerNameObject) {
        env->DeleteLocalRef(playerNameObject);
    }
    if (teamNameObject) {
        env->DeleteLocalRef(teamNameObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
    return added;
}

bool Scoreboard::RemovePlayerFromTeams(JNIEnv* env, const std::string& playerName) {
    if (!env || !this || playerName.empty()) {
        return false;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return false;
    }

    const std::string methodName = Mapper::Get("removePlayerFromTeams");
    Method* method = methodName.empty() ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), "(Ljava/lang/String;)Z");

    jstring playerNameObject = env->NewStringUTF(playerName.c_str());
    const bool removed = (method && playerNameObject)
        ? method->CallBooleanMethod(env, this, false, playerNameObject)
        : false;
    if (playerNameObject) {
        env->DeleteLocalRef(playerNameObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
    return removed;
}

void Scoreboard::RemoveTeam(JNIEnv* env, jobject teamObject) {
    if (!env || !this || !teamObject) {
        return;
    }

    auto* scoreboardClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!scoreboardClass) {
        return;
    }

    const std::string methodName = Mapper::Get("removeTeam");
    const std::string teamSignature = Mapper::Get("net/minecraft/scoreboard/ScorePlayerTeam", 2);
    const std::string signature = "(" + teamSignature + ")V";
    Method* method = (methodName.empty() || teamSignature.empty()) ? nullptr : scoreboardClass->GetMethod(env, methodName.c_str(), signature.c_str());
    if (method) {
        method->CallVoidMethod(env, this, false, teamObject);
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(scoreboardClass));
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
