#pragma once

#include <jni.h>
#include <string>

class Scoreboard {
public:
    jobject GetTeam(JNIEnv* env, const std::string& teamName);
    jobject GetPlayersTeam(JNIEnv* env, const std::string& playerName);
    jobject CreateTeam(JNIEnv* env, const std::string& teamName);
    bool AddPlayerToTeam(JNIEnv* env, const std::string& playerName, const std::string& teamName);
    bool RemovePlayerFromTeams(JNIEnv* env, const std::string& playerName);
    void RemoveTeam(JNIEnv* env, jobject teamObject);
    jobject GetObjectiveInDisplaySlot(JNIEnv* env, int slot);
    jobject GetValueFromObjective(JNIEnv* env, const std::string& playerName, jobject objective);
    static int GetScorePoints(JNIEnv* env, jobject score);
};
