#include "pch.h"
#include "AxisAlignedBB.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../mapping/Mapper.h"

AxisAlignedBB_t AxisAlignedBB::GetNativeBoundingBox(JNIEnv* env) {
    if (!env || this == nullptr) {
        return {};
    }

    auto* axisClass = reinterpret_cast<Class*>(env->GetObjectClass(reinterpret_cast<jobject>(this)));
    if (!axisClass) {
        return {};
    }

    Field* minXField = axisClass->GetField(env, Mapper::Get("minX").c_str(), "D");
    if (!minXField) minXField = axisClass->GetField(env, "field_72340_a", "D");
    if (!minXField) minXField = axisClass->GetField(env, "a", "D");

    Field* minYField = axisClass->GetField(env, Mapper::Get("minY").c_str(), "D");
    if (!minYField) minYField = axisClass->GetField(env, "field_72338_b", "D");
    if (!minYField) minYField = axisClass->GetField(env, "b", "D");

    Field* minZField = axisClass->GetField(env, Mapper::Get("minZ").c_str(), "D");
    if (!minZField) minZField = axisClass->GetField(env, "field_72339_c", "D");
    if (!minZField) minZField = axisClass->GetField(env, "c", "D");

    Field* maxXField = axisClass->GetField(env, Mapper::Get("maxX").c_str(), "D");
    if (!maxXField) maxXField = axisClass->GetField(env, "field_72336_d", "D");
    if (!maxXField) maxXField = axisClass->GetField(env, "d", "D");

    Field* maxYField = axisClass->GetField(env, Mapper::Get("maxY").c_str(), "D");
    if (!maxYField) maxYField = axisClass->GetField(env, "field_72337_e", "D");
    if (!maxYField) maxYField = axisClass->GetField(env, "e", "D");

    Field* maxZField = axisClass->GetField(env, Mapper::Get("maxZ").c_str(), "D");
    if (!maxZField) maxZField = axisClass->GetField(env, "field_72334_f", "D");
    if (!maxZField) maxZField = axisClass->GetField(env, "f", "D");

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

    Field* minXField = axisClass->GetField(env, Mapper::Get("minX").c_str(), "D");
    if (!minXField) minXField = axisClass->GetField(env, "field_72340_a", "D");
    if (!minXField) minXField = axisClass->GetField(env, "a", "D");

    Field* minYField = axisClass->GetField(env, Mapper::Get("minY").c_str(), "D");
    if (!minYField) minYField = axisClass->GetField(env, "field_72338_b", "D");
    if (!minYField) minYField = axisClass->GetField(env, "b", "D");

    Field* minZField = axisClass->GetField(env, Mapper::Get("minZ").c_str(), "D");
    if (!minZField) minZField = axisClass->GetField(env, "field_72339_c", "D");
    if (!minZField) minZField = axisClass->GetField(env, "c", "D");

    Field* maxXField = axisClass->GetField(env, Mapper::Get("maxX").c_str(), "D");
    if (!maxXField) maxXField = axisClass->GetField(env, "field_72336_d", "D");
    if (!maxXField) maxXField = axisClass->GetField(env, "d", "D");

    Field* maxYField = axisClass->GetField(env, Mapper::Get("maxY").c_str(), "D");
    if (!maxYField) maxYField = axisClass->GetField(env, "field_72337_e", "D");
    if (!maxYField) maxYField = axisClass->GetField(env, "e", "D");

    Field* maxZField = axisClass->GetField(env, Mapper::Get("maxZ").c_str(), "D");
    if (!maxZField) maxZField = axisClass->GetField(env, "field_72334_f", "D");
    if (!maxZField) maxZField = axisClass->GetField(env, "f", "D");

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
