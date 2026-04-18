#include "pch.h"
#include "GameInstance.h"
#include "Field.h"
#include "Method.h"

#include <algorithm>

typedef jint(JNICALL* JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);

GameInstance* g_Game = nullptr;

namespace {
    std::string NormalizeClassName(std::string className) {
        std::replace(className.begin(), className.end(), '/', '.');
        return className;
    }
}

GameInstance::GameInstance()
{
}

JNIEnv* GameInstance::GetCurrentEnv() const
{
	if (!m_Jvm)
		return nullptr;

	JNIEnv* env = nullptr;
	jint result = m_Jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
	if (result == JNI_EDETACHED) {
		result = m_Jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr);
	}

	return result == JNI_OK ? env : nullptr;
}

bool GameInstance::Attach()
{
	HMODULE jvmDll = GetModuleHandleA("jvm.dll");
	if (!jvmDll)
		return false;

	auto pGetCreatedJavaVMs = (JNI_GetCreatedJavaVMs_t)GetProcAddress(jvmDll, "JNI_GetCreatedJavaVMs");
	if (!pGetCreatedJavaVMs)
		return false;

	jsize vmCount = 0;
	if (pGetCreatedJavaVMs(&m_Jvm, 1, &vmCount) != JNI_OK || vmCount == 0 || !m_Jvm)
		return false;

	jint res = m_Jvm->GetEnv(reinterpret_cast<void**>(&m_Env), JNI_VERSION_1_6);
	if (res == JNI_EDETACHED) {
		res = m_Jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&m_Env), nullptr);
		if (res != JNI_OK || !m_Env)
			return false;
	}
	else if (res != JNI_OK || !m_Env) {
		return false;
	}

	if (m_Jvm->GetEnv(reinterpret_cast<void**>(&m_Jvmti), JVMTI_VERSION_1_1) != JNI_OK) {
		m_Jvmti = nullptr;
	}

	return true;
}

void GameInstance::Detach()
{
	JNIEnv* env = GetCurrentEnv();
	if (env) {
		std::lock_guard<std::mutex> lock(m_CacheMutex);
		for (auto& [name, klass] : m_CachedClass) {
			if (klass) {
				env->DeleteGlobalRef(reinterpret_cast<jobject>(klass));
			}
		}
		m_CachedClass.clear();
	}

	if (m_Jvm) {
		m_Jvm->DetachCurrentThread();
		m_Env = nullptr;
		m_Jvmti = nullptr;
	}
}

bool GameInstance::PopulateClassCache()
{
	if (!m_Env || !m_Jvmti)
		return false;

	jclass* classes = nullptr;
	jint classCount = 0;
	if (m_Jvmti->GetLoadedClasses(&classCount, &classes) != JVMTI_ERROR_NONE || !classes || classCount <= 0) {
		return false;
	}

	for (int index = 0; index < classCount; ++index) {
		char* signature = nullptr;
		if (m_Jvmti->GetClassSignature(classes[index], &signature, nullptr) != JVMTI_ERROR_NONE || !signature) {
			if (classes[index]) {
				m_Env->DeleteLocalRef(classes[index]);
			}
			continue;
		}

		std::string className(signature);
		m_Jvmti->Deallocate(reinterpret_cast<unsigned char*>(signature));

		if (className.length() > 2 && className.front() == 'L' && className.back() == ';') {
			className = className.substr(1, className.length() - 2);
			className = NormalizeClassName(className);

			std::lock_guard<std::mutex> lock(m_CacheMutex);
			if (!m_CachedClass.contains(className)) {
				auto* globalClass = reinterpret_cast<Class*>(m_Env->NewGlobalRef(classes[index]));
				if (globalClass) {
					m_CachedClass.emplace(className, globalClass);
				}
			}
		}

		if (classes[index]) {
			m_Env->DeleteLocalRef(classes[index]);
		}
	}

	m_Jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));
	return true;
}

GameVersions GameInstance::DetectGameVersion()
{
	if (!m_Env)
		return UNKNOWN;

	HWND currentWindowHandle = NULL;
	for (HWND hWnd = GetTopWindow(NULL); hWnd != NULL; hWnd = GetNextWindow(hWnd, GW_HWNDNEXT))
	{
		if (!IsWindowVisible(hWnd))
			continue;

		int length = GetWindowTextLengthA(hWnd);
		if (length == 0)
			continue;

		CHAR cName[MAX_PATH];
		GetClassNameA(hWnd, cName, _countof(cName));
		if (strcmp(cName, "LWJGL") != 0)
			continue;

		DWORD pid;
		GetWindowThreadProcessId(hWnd, &pid);
		if (pid == GetCurrentProcessId()) {
			currentWindowHandle = hWnd;
			break;
		}
	}

	if (!currentWindowHandle)
		return UNKNOWN;

	char windowTitle[256] = {};
	GetWindowTextA(currentWindowHandle, windowTitle, sizeof(windowTitle));
	std::string title(windowTitle);

	bool vanillaMappings = title.find("Badlion Minecraft") != std::string::npos;

	if (title.find("Lunar Client") != std::string::npos)
		return LUNAR;

	Class* launchWrapper = FindClass("net.minecraft.launchwrapper.LaunchClassLoader");
	Class* launchClass = FindClass("net.minecraft.launchwrapper.Launch");

	bool hasLaunch = false;
	if (launchClass && launchWrapper) {
		Field* classLoaderField = launchClass->GetField(m_Env, "classLoader", "Lnet/minecraft/launchwrapper/LaunchClassLoader;", true);
		if (classLoaderField) {
			hasLaunch = true;
		}
	}

	GameVersions version = UNKNOWN;
	if (hasLaunch && !vanillaMappings)
	{
		version = FORGE_1_8;
		if (!FindClass("net.minecraft.client.Minecraft")) {
			version = BADLION;
		}
	}
	else {
		version = BADLION;
	}

	if (FindClass("net.digitalingot.featheropt.FeatherCoreMod")) {
		version = FEATHER_1_8;
	}

	return version;
}

Class* GameInstance::FindClass(const std::string& className) const
{
	const std::string normalizedClassName = NormalizeClassName(className);

	{
		std::lock_guard<std::mutex> lock(m_CacheMutex);
		auto it = m_CachedClass.find(normalizedClassName);
		if (it != m_CachedClass.end())
			return it->second;
	}

	JNIEnv* env = GetCurrentEnv();
	if (!env)
		return nullptr;

	std::string jniName = normalizedClassName;
	std::replace(jniName.begin(), jniName.end(), '.', '/');

	jclass localClass = env->FindClass(jniName.c_str());
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return nullptr;
	}
	if (!localClass)
		return nullptr;

	auto* klass = reinterpret_cast<Class*>(env->NewGlobalRef(localClass));
	env->DeleteLocalRef(localClass);
	if (!klass) {
		return nullptr;
	}

	{
		std::lock_guard<std::mutex> lock(m_CacheMutex);
		const_cast<GameInstance*>(this)->m_CachedClass[normalizedClassName] = klass;
	}

	return klass;
}

bool GameInstance::InitializeGame()
{
	PopulateClassCache();

	m_GameVersion = DetectGameVersion();
	if (m_GameVersion == UNKNOWN)
		return false;

	Mapper::Initialize(m_GameVersion);
	PopulateClassCache();

	auto mcClassName = Mapper::Get("net/minecraft/client/Minecraft");
	if (mcClassName.empty())
		return false;

	auto* mcClass = FindClass(mcClassName);
	if (!mcClass)
		return false;

	m_Initialized = true;
	return true;
}
