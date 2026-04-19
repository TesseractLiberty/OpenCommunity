#include "pch.h"
#include "AxisAlignedBB.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../mapping/Mapper.h"

namespace {
    Field* GetBoundingBoxField(JNIEnv* env, Class* axisClass, const char* mappingKey) {
        if (!env || !axisClass) {
            return nullptr;
        }

        const std::string fieldName = Mapper::Get(mappingKey);
        return fieldName.empty() ? nullptr : axisClass->GetField(env, fieldName.c_str(), "D");
    }
}

AxisAlignedBB_t AxisAlignedBB::GetNativeBoundingBox(JNIEnv* env) {
    if (!env || this == nullptr) {
        return {};
    }

    auto* axisClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!axisClass) {
        return {};
    }

    Field* minXField = GetBoundingBoxField(env, axisClass, "minX");
    Field* minYField = GetBoundingBoxField(env, axisClass, "minY");
    Field* minZField = GetBoundingBoxField(env, axisClass, "minZ");
    Field* maxXField = GetBoundingBoxField(env, axisClass, "maxX");
    Field* maxYField = GetBoundingBoxField(env, axisClass, "maxY");
    Field* maxZField = GetBoundingBoxField(env, axisClass, "maxZ");

    AxisAlignedBB_t result{};

    try {
        if (minXField && minYField && minZField && maxXField && maxYField && maxZField) {
            result = AxisAlignedBB_t{
                static_cast<float>(minXField->GetDoubleField(env, reinterpret_cast<jobject>(this))),
                static_cast<float>(minYField->GetDoubleField(env, reinterpret_cast<jobject>(this))),
                static_cast<float>(minZField->GetDoubleField(env, reinterpret_cast<jobject>(this))),
                static_cast<float>(maxXField->GetDoubleField(env, reinterpret_cast<jobject>(this))),
                static_cast<float>(maxYField->GetDoubleField(env, reinterpret_cast<jobject>(this))),
                static_cast<float>(maxZField->GetDoubleField(env, reinterpret_cast<jobject>(this)))
            };
        }
    } catch (...) {
        result = {};
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(axisClass));
    return result;
}

void AxisAlignedBB::SetNativeBoundingBox(AxisAlignedBB_t buffer, JNIEnv* env) {
    if (!env || this == nullptr) {
        return;
    }

    auto* axisClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!axisClass) {
        return;
    }

    Field* minXField = GetBoundingBoxField(env, axisClass, "minX");
    Field* minYField = GetBoundingBoxField(env, axisClass, "minY");
    Field* minZField = GetBoundingBoxField(env, axisClass, "minZ");
    Field* maxXField = GetBoundingBoxField(env, axisClass, "maxX");
    Field* maxYField = GetBoundingBoxField(env, axisClass, "maxY");
    Field* maxZField = GetBoundingBoxField(env, axisClass, "maxZ");

    try {
        if (minXField && minYField && minZField && maxXField && maxYField && maxZField) {
            minXField->SetDoubleField(env, reinterpret_cast<jobject>(this), buffer.minX);
            minYField->SetDoubleField(env, reinterpret_cast<jobject>(this), buffer.minY);
            minZField->SetDoubleField(env, reinterpret_cast<jobject>(this), buffer.minZ);
            maxXField->SetDoubleField(env, reinterpret_cast<jobject>(this), buffer.maxX);
            maxYField->SetDoubleField(env, reinterpret_cast<jobject>(this), buffer.maxY);
            maxZField->SetDoubleField(env, reinterpret_cast<jobject>(this), buffer.maxZ);
        }
    } catch (...) {
    }

    env->DeleteLocalRef(reinterpret_cast<jclass>(axisClass));
}
