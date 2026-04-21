#include "pch.h"
#include "Field.h"

jobject Field::GetObjectField(JNIEnv* env, void* fieldOwner, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return NULL;

	jobject res = staticField ? env->GetStaticObjectField((jclass)fieldOwner, (jfieldID)this) : env->GetObjectField((jobject)fieldOwner, (jfieldID)this);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return NULL;
	}
	return res;
}

float Field::GetFloatField(JNIEnv* env, void* fieldOwner, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return 0.0f;

	float res = staticField ? env->GetStaticFloatField((jclass)fieldOwner, (jfieldID)this) : env->GetFloatField((jobject)fieldOwner, (jfieldID)this);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return 0.0f;
	}
	return res;
}

double Field::GetDoubleField(JNIEnv* env, void* fieldOwner, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return 0.0;

	double res = staticField ? env->GetStaticDoubleField((jclass)fieldOwner, (jfieldID)this) : env->GetDoubleField((jobject)fieldOwner, (jfieldID)this);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return 0.0;
	}
	return res;
}

int Field::GetIntField(JNIEnv* env, void* fieldOwner, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return 0;

	int res = staticField ? env->GetStaticIntField((jclass)fieldOwner, (jfieldID)this) : env->GetIntField((jobject)fieldOwner, (jfieldID)this);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return 0;
	}
	return res;
}

bool Field::GetBooleanField(JNIEnv* env, void* fieldOwner, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return false;

	bool res = staticField ? env->GetStaticBooleanField((jclass)fieldOwner, (jfieldID)this) : env->GetBooleanField((jobject)fieldOwner, (jfieldID)this);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	return res;
}

void Field::SetObjectField(JNIEnv* env, void* fieldOwner, jobject buffer, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return;

	staticField ? env->SetStaticObjectField((jclass)fieldOwner, (jfieldID)this, buffer) : env->SetObjectField((jobject)fieldOwner, (jfieldID)this, buffer);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
}

void Field::SetFloatField(JNIEnv* env, void* fieldOwner, float buffer, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return;

	staticField ? env->SetStaticFloatField((jclass)fieldOwner, (jfieldID)this, buffer) : env->SetFloatField((jobject)fieldOwner, (jfieldID)this, buffer);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
}

void Field::SetDoubleField(JNIEnv* env, void* fieldOwner, double buffer, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return;

	staticField ? env->SetStaticDoubleField((jclass)fieldOwner, (jfieldID)this, buffer) : env->SetDoubleField((jobject)fieldOwner, (jfieldID)this, buffer);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
}

void Field::SetIntField(JNIEnv* env, void* fieldOwner, int buffer, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return;

	staticField ? env->SetStaticIntField((jclass)fieldOwner, (jfieldID)this, buffer) : env->SetIntField((jobject)fieldOwner, (jfieldID)this, buffer);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
}

void Field::SetBooleanField(JNIEnv* env, void* fieldOwner, bool buffer, bool staticField)
{
	if (this == NULL || fieldOwner == NULL || env == NULL)
		return;

	staticField ? env->SetStaticBooleanField((jclass)fieldOwner, (jfieldID)this, buffer) : env->SetBooleanField((jobject)fieldOwner, (jfieldID)this, buffer);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
	}
}
