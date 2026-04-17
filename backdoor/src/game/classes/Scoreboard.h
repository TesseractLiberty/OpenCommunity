#pragma once

#include <jni.h>
#include <string>

class Scoreboard {
public:
    jobject GetPlayersTeam(JNIEnv* env, const std::string& playerName);
    jobject GetObjectiveInDisplaySlot(JNIEnv* env, int slot);
    jobject GetValueFromObjective(JNIEnv* env, const std::string& playerName, jobject objective);
    static int GetScorePoints(JNIEnv* env, jobject score);
};
