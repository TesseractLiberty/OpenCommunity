#include "pch.h"
#include "Class.h"
#include "Field.h"
#include "Method.h"

#include <mutex>

static std::mutex g_ClassMutex;

std::string Class::GetName(JNIEnv* env)
{
	if (this == NULL || env == NULL)
		return "";

	static jclass cls = nullptr;
	static jmethodID mid_getName = nullptr;

	{
		std::lock_guard<std::mutex> lock(g_ClassMutex);
		if (!cls) {
			jclass localCls = env->FindClass("java/lang/Class");
			if (localCls) {
				cls = (jclass)env->NewGlobalRef(localCls);
				mid_getName = env->GetMethodID(cls, "getName", "()Ljava/lang/String;");
				env->DeleteLocalRef(localCls);
			}
		}
	}

	if (!cls || !mid_getName)
		return "";

	const auto jname = (jstring)env->CallObjectMethod((jclass)this, mid_getName);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return "";
	}

	if (jname) {
		const char* name = env->GetStringUTFChars(jname, 0);
		std::string result(name);
		env->ReleaseStringUTFChars(jname, name);
		env->DeleteLocalRef(jname);
		return result;
	}
	return "";
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
