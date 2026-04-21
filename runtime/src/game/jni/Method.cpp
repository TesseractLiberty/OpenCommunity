#include "pch.h"
#include "Method.h"

jobject Method::CallObjectMethod(JNIEnv* env, void* methodOwner, bool staticMethod, ...)
{
	if (this == NULL || methodOwner == NULL || env == NULL)
		return NULL;

	va_list args;
	va_start(args, staticMethod);
	const auto ret = staticMethod ? env->CallStaticObjectMethodV((jclass)methodOwner, (jmethodID)this, args) : env->CallObjectMethodV((jobject)methodOwner, (jmethodID)this, args);
	va_end(args);

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return NULL;
	}

	return ret;
}

float Method::CallFloatMethod(JNIEnv* env, void* methodOwner, bool staticMethod, ...)
{
	if (this == NULL || methodOwner == NULL || env == NULL)
		return 0.0f;

	va_list args;
	va_start(args, staticMethod);
	const auto ret = staticMethod ? env->CallStaticFloatMethodV((jclass)methodOwner, (jmethodID)this, args) : env->CallFloatMethodV((jobject)methodOwner, (jmethodID)this, args);
	va_end(args);

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return 0.0f;
	}

	return ret;
}

void Method::CallVoidMethod(JNIEnv* env, void* methodOwner, bool staticMethod, ...)
{
	if (this == NULL || methodOwner == NULL || env == NULL)
		return;

	va_list args;
	va_start(args, staticMethod);
	staticMethod ? env->CallStaticVoidMethodV((jclass)methodOwner, (jmethodID)this, args) : env->CallVoidMethodV((jobject)methodOwner, (jmethodID)this, args);
	va_end(args);

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
}

int Method::CallIntMethod(JNIEnv* env, void* methodOwner, bool staticMethod, ...)
{
	if (this == NULL || methodOwner == NULL || env == NULL)
		return 0;

	va_list args;
	va_start(args, staticMethod);
	const auto ret = staticMethod ? env->CallStaticIntMethodV((jclass)methodOwner, (jmethodID)this, args) : env->CallIntMethodV((jobject)methodOwner, (jmethodID)this, args);
	va_end(args);

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return 0;
	}

	return ret;
}

bool Method::CallBooleanMethod(JNIEnv* env, void* methodOwner, bool staticMethod, ...)
{
	if (this == NULL || methodOwner == NULL || env == NULL)
		return false;

	va_list args;
	va_start(args, staticMethod);
	const auto ret = staticMethod ? env->CallStaticBooleanMethodV((jclass)methodOwner, (jmethodID)this, args) : env->CallBooleanMethodV((jobject)methodOwner, (jmethodID)this, args);
	va_end(args);

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}

	return ret;
}

void Method::CallStaticVoidMethod(JNIEnv* env, void* methodOwner, ...)
{
	if (this == NULL || methodOwner == NULL || env == NULL)
		return;

	va_list args;
	va_start(args, methodOwner);
	env->CallStaticVoidMethodV((jclass)methodOwner, (jmethodID)this, args);
	va_end(args);

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
}
