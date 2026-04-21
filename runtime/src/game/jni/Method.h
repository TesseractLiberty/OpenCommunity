#pragma once

#include <jni.h>

class Method
{
public:
	jobject CallObjectMethod(JNIEnv* env, void* methodOwner, bool staticMethod = false, ...);
	float CallFloatMethod(JNIEnv* env, void* methodOwner, bool staticMethod = false, ...);
	void CallVoidMethod(JNIEnv* env, void* methodOwner, bool staticMethod = false, ...);
	int CallIntMethod(JNIEnv* env, void* methodOwner, bool staticMethod = false, ...);
	bool CallBooleanMethod(JNIEnv* env, void* methodOwner, bool staticMethod = false, ...);
	void CallStaticVoidMethod(JNIEnv* env, void* methodOwner, ...);
};
