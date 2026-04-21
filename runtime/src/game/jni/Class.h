#pragma once

#include <string>
#include <jni.h>

class Field;
class Method;
class Class
{
public:
	std::string GetName(JNIEnv* env);
	Field* GetField(JNIEnv* env, const char* name, const char* sig, bool staticField = false);
	Method* GetMethod(JNIEnv* env, const char* name, const char* sig, bool staticMethod = false);
};
