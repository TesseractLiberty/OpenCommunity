#include "pch.h"
#include "ActiveRenderInfo.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../jni/Method.h"
#include "../mapping/Mapper.h"

namespace {
    std::vector<float> ReadFloatBuffer(JNIEnv* env, jobject bufferObject) {
        if (!env || !bufferObject) {
            return {};
        }

        std::vector<float> values;
        float* directData = static_cast<float*>(env->GetDirectBufferAddress(bufferObject));
        if (directData) {
            const jlong capacity = env->GetDirectBufferCapacity(bufferObject);
            if (capacity >= 16) {
                values.assign(directData, directData + 16);
                return values;
            }
        }

        Class* floatBufferClass = g_Game ? g_Game->FindClass("java/nio/FloatBuffer") : nullptr;
        Method* getMethod = floatBufferClass ? floatBufferClass->GetMethod(env, "get", "(I)F") : nullptr;
        if (!getMethod) {
            return {};
        }

        values.reserve(16);
        for (int index = 0; index < 16; ++index) {
            values.push_back(getMethod->CallFloatMethod(env, bufferObject, false, index));
        }
        return values;
    }

    std::vector<float> ReadMatrixField(JNIEnv* env, const char* fieldKey) {
        if (!env || !g_Game || !g_Game->IsInitialized() || !fieldKey) {
            return {};
        }

        const std::string className = Mapper::Get("net/minecraft/client/renderer/ActiveRenderInfo");
        const std::string fieldName = Mapper::Get(fieldKey);
        if (className.empty() || fieldName.empty()) {
            return {};
        }

        Class* activeRenderInfoClass = g_Game->FindClass(className);
        if (!activeRenderInfoClass) {
            return {};
        }

        Field* matrixField = activeRenderInfoClass->GetField(env, fieldName.c_str(), "Ljava/nio/FloatBuffer;", true);
        if (!matrixField) {
            return {};
        }

        jobject bufferObject = matrixField->GetObjectField(env, activeRenderInfoClass, true);
        if (!bufferObject) {
            return {};
        }

        std::vector<float> matrix = ReadFloatBuffer(env, bufferObject);
        env->DeleteLocalRef(bufferObject);
        return matrix;
    }
}

std::vector<float> ActiveRenderInfo::GetProjection(JNIEnv* env) {
    return ReadMatrixField(env, "PROJECTION");
}

std::vector<float> ActiveRenderInfo::GetModelView(JNIEnv* env) {
    return ReadMatrixField(env, "MODELVIEW");
}
