#include "pch.h"
#include "GameInstance.h"
#include "Field.h"
#include "Method.h"

typedef jint(JNICALL* JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);

GameInstance* g_Game = nullptr;

GameInstance::GameInstance()
{
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

	return true;
}

void GameInstance::Detach()
{
	if (m_Jvm) {
		m_Jvm->DetachCurrentThread();
		m_Env = nullptr;
	}
}

bool GameInstance::PopulateClassCache()
{
	if (!m_Env)
		return false;

	jclass classClass = m_Env->FindClass("java/lang/Class");
	if (!classClass || m_Env->ExceptionCheck()) {
		if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();
		return false;
	}

	jmethodID getClassLoader = m_Env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
	jmethodID getName = m_Env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
	if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();

	jclass threadClass = m_Env->FindClass("java/lang/Thread");
	if (!threadClass || m_Env->ExceptionCheck()) {
		if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();
		m_Env->DeleteLocalRef(classClass);
		return false;
	}

	jmethodID getAllStackTraces = m_Env->GetStaticMethodID(threadClass, "getAllStackTraces", "()Ljava/util/Map;");
	if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();


	jclass clClass = m_Env->FindClass("java/lang/ClassLoader");
	if (!clClass || m_Env->ExceptionCheck()) {
		if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();
	}

	m_Env->DeleteLocalRef(classClass);
	m_Env->DeleteLocalRef(threadClass);
	if (clClass) m_Env->DeleteLocalRef(clClass);

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

	bool is1_7 = title.find("1.7.10") != std::string::npos;
	bool vanillaMappings = title.find("Badlion Minecraft") != std::string::npos;

	if (title.find("Lunar Client") != std::string::npos)
		return is1_7 ? LUNAR_1_7_10 : LUNAR_1_8;

	// Check for LaunchWrapper (Forge)
	jclass launchWrapper = m_Env->FindClass("net/minecraft/launchwrapper/LaunchClassLoader");
	if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();
	jclass launchClazz = m_Env->FindClass("net/minecraft/launchwrapper/Launch");
	if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();

	bool hasLaunch = false;
	if (launchClazz && launchWrapper) {
		jfieldID classLoaderField = m_Env->GetStaticFieldID(launchClazz, "classLoader", "Lnet/minecraft/launchwrapper/LaunchClassLoader;");
		if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();
		if (classLoaderField) hasLaunch = true;
	}

	GameVersions version = UNKNOWN;
	if (hasLaunch && !vanillaMappings)
	{
		version = is1_7 ? FORGE_1_7_10 : FORGE_1_8;
		jclass mcClass = m_Env->FindClass("net/minecraft/client/Minecraft");
		if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();
		if (!mcClass) {
			version = is1_7 ? CASUAL_1_7_10 : CASUAL_1_8;
		}
		if (mcClass) m_Env->DeleteLocalRef(mcClass);
	}
	else {
		version = is1_7 ? CASUAL_1_7_10 : CASUAL_1_8;
	}

	// Check for Feather
	jclass featherClass = m_Env->FindClass("net/digitalingot/featheropt/FeatherCoreMod");
	if (m_Env->ExceptionCheck()) m_Env->ExceptionClear();
	if (featherClass) {
		version = FEATHER_1_8;
		m_Env->DeleteLocalRef(featherClass);
	}

	if (launchWrapper) m_Env->DeleteLocalRef(launchWrapper);
	if (launchClazz) m_Env->DeleteLocalRef(launchClazz);

	return version;
}

Class* GameInstance::FindClass(const std::string& className) const
{
	if (!m_Env)
		return nullptr;

	{
		std::lock_guard<std::mutex> lock(m_CacheMutex);
		auto it = m_CachedClass.find(className);
		if (it != m_CachedClass.end())
			return it->second;
	}

	std::string jniName = className;
	for (auto& c : jniName) {
		if (c == '.') c = '/';
	}

	jclass localClass = m_Env->FindClass(jniName.c_str());
	if (m_Env->ExceptionCheck()) {
		m_Env->ExceptionClear();
		return nullptr;
	}
	if (!localClass)
		return nullptr;

	auto* cls = (Class*)m_Env->NewGlobalRef(localClass);
	m_Env->DeleteLocalRef(localClass);

	{
		std::lock_guard<std::mutex> lock(m_CacheMutex);
		const_cast<GameInstance*>(this)->m_CachedClass[className] = cls;
	}

	return cls;
}

bool GameInstance::InitializeGame()
{
	m_GameVersion = DetectGameVersion();
	if (m_GameVersion == UNKNOWN)
		return false;

	Mapper::Initialize(m_GameVersion);

	auto mcClassName = Mapper::Get("net/minecraft/client/Minecraft");
	if (mcClassName.empty())
		return false;

	auto* mcClass = FindClass(mcClassName);
	if (!mcClass)
		return false;

	m_Initialized = true;
	return true;
}
