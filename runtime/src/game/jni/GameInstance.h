#pragma once

#include "Class.h"
#include "../mapping/Mapper.h"

#include <jni.h>
#include <jvmti.h>
#include <unordered_map>
#include <mutex>
#include <string>

class GameInstance
{
public:
	GameInstance();
	~GameInstance() = default;

	auto GetJVM() const { return m_Jvm; }
	auto GetENV() const { return m_Env; }
	auto GetJVMTI() const { return m_Jvmti; }
	auto GetGameVersion() const { return m_GameVersion; }
	bool IsInitialized() const { return m_Initialized; }

	JNIEnv* GetCurrentEnv() const;
	bool Attach();
	void Detach();
	bool InitializeGame();
	Class* FindClass(const std::string& className) const;

private:
	bool PopulateClassCache();
	GameVersions DetectGameVersion();

	bool m_Initialized = false;
	GameVersions m_GameVersion = UNKNOWN;

	std::unordered_map<std::string, Class*> m_CachedClass;
	mutable std::mutex m_CacheMutex;

	JavaVM* m_Jvm = nullptr;
	JNIEnv* m_Env = nullptr;
	jvmtiEnv* m_Jvmti = nullptr;
};

extern GameInstance* g_Game;
