#include "pch.h"
#include "Class.h"
#include "Field.h"
#include "Method.h"

#include <mutex>

static std::mutex g_ClassMutex;
static jclass g_JavaClassClass = nullptr;
static jmethodID g_JavaClassGetName = nullptr;

std::string Class::GetName(JNIEnv* env)
{
	if (this == NULL || env == NULL)
		return "";

	{
		std::lock_guard<std::mutex> lock(g_ClassMutex);
		if (!g_JavaClassClass) {
			jclass localCls = env->FindClass("java/lang/Class");
			if (env->ExceptionCheck()) {
				env->ExceptionClear();
			}
			if (localCls) {
				g_JavaClassClass = (jclass)env->NewGlobalRef(localCls);
				g_JavaClassGetName = env->GetMethodID(g_JavaClassClass, "getName", "()Ljava/lang/String;");
				env->DeleteLocalRef(localCls);
			}
		}
	}

	if (!g_JavaClassClass || !g_JavaClassGetName)
		return "";

	const auto jname = (jstring)env->CallObjectMethod((jclass)this, g_JavaClassGetName);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		if (jname) {
			env->DeleteLocalRef(jname);
		}
		return "";
	}

	if (jname) {
		const char* name = env->GetStringUTFChars(jname, 0);
		if (!name) {
			env->DeleteLocalRef(jname);
			return "";
		}
		std::string result(name);
		env->ReleaseStringUTFChars(jname, name);
		env->DeleteLocalRef(jname);
		return result;
	}
	return "";
}

void Class::ReleaseCachedRefs(JNIEnv* env)
{
	if (!env)
		return;

	std::lock_guard<std::mutex> lock(g_ClassMutex);
	if (g_JavaClassClass) {
		env->DeleteGlobalRef(g_JavaClassClass);
		g_JavaClassClass = nullptr;
		g_JavaClassGetName = nullptr;
	}
}

Field* Class::GetField(JNIEnv* env, const char* name, const char* sig, bool staticField)
{
	if (this == NULL || env == NULL)
		return NULL;

	jfieldID fid = staticField ? env->GetStaticFieldID((jclass)this, name, sig) : env->GetFieldID((jclass)this, name, sig);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return NULL;
	}
	return (Field*)fid;
}

Method* Class::GetMethod(JNIEnv* env, const char* name, const char* sig, bool staticMethod)
{
	if (this == NULL || env == NULL)
		return NULL;

	jmethodID mid = staticMethod ? env->GetStaticMethodID((jclass)this, name, sig) : env->GetMethodID((jclass)this, name, sig);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return NULL;
	}
	return (Method*)mid;
}
