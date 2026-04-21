#pragma once

#include <jni.h>

class Field
{
public:
	jobject GetObjectField(JNIEnv* env, void* fieldOwner, bool staticField = false);
	float GetFloatField(JNIEnv* env, void* fieldOwner, bool staticField = false);
	double GetDoubleField(JNIEnv* env, void* fieldOwner, bool staticField = false);
	int GetIntField(JNIEnv* env, void* fieldOwner, bool staticField = false);
	bool GetBooleanField(JNIEnv* env, void* fieldOwner, bool staticField = false);

	void SetObjectField(JNIEnv* env, void* fieldOwner, jobject buffer, bool staticField = false);
	void SetFloatField(JNIEnv* env, void* fieldOwner, float buffer, bool staticField = false);
	void SetDoubleField(JNIEnv* env, void* fieldOwner, double buffer, bool staticField = false);
	void SetIntField(JNIEnv* env, void* fieldOwner, int buffer, bool staticField = false);
	void SetBooleanField(JNIEnv* env, void* fieldOwner, bool buffer, bool staticField = false);
};
