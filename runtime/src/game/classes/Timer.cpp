#include "pch.h"
#include "Timer.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../mapping/Mapper.h"

float Timer::GetRenderPartialTicks(JNIEnv* env) {
    if (!this || !env) {
        return 0.0f;
    }

    auto* timerClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!timerClass) {
        return 0.0f;
    }

    Field* partialTicksField = timerClass->GetField(env, Mapper::Get("renderPartialTicks").c_str(), "F");
    const float partialTicks = partialTicksField ? partialTicksField->GetFloatField(env, this) : 0.0f;
    env->DeleteLocalRef(reinterpret_cast<jclass>(timerClass));
    return partialTicks;
}
