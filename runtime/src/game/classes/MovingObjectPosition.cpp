#include "pch.h"
#include "MovingObjectPosition.h"

#include "../jni/Class.h"
#include "../jni/Field.h"
#include "../jni/GameInstance.h"
#include "../mapping/Mapper.h"

bool MovingObjectPosition::IsAimingEntity(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return false;
    }

    const std::string className = Mapper::Get("net/minecraft/util/MovingObjectPosition");
    const std::string typeClassName = Mapper::Get("net/minecraft/util/MovingObjectPosition$MovingObjectType");
    const std::string typeFieldName = Mapper::Get("typeOfHit");
    const std::string entityFieldName = Mapper::Get("ENTITY");
    if (className.empty() || typeClassName.empty() || typeFieldName.empty() || entityFieldName.empty()) {
        return false;
    }

    Class* movingObjectClass = g_Game->FindClass(className);
    Class* movingTypeClass = g_Game->FindClass(typeClassName);
    if (!movingObjectClass || !movingTypeClass) {
        return false;
    }

    const std::string typeSignature = Mapper::Get("net/minecraft/util/MovingObjectPosition$MovingObjectType", 2);
    Field* typeField = movingObjectClass->GetField(env, typeFieldName.c_str(), typeSignature.c_str());
    Field* entityTypeField = movingTypeClass->GetField(env, entityFieldName.c_str(), typeSignature.c_str(), true);
    if (!typeField || !entityTypeField) {
        return false;
    }

    jobject typeValue = typeField->GetObjectField(env, this);
    jobject entityValue = entityTypeField->GetObjectField(env, movingTypeClass, true);
    const bool result = typeValue && entityValue && env->IsSameObject(typeValue, entityValue);
    if (typeValue) {
        env->DeleteLocalRef(typeValue);
    }
    if (entityValue) {
        env->DeleteLocalRef(entityValue);
    }
    return result;
}

jobject MovingObjectPosition::GetEntity(JNIEnv* env) {
    if (!env || !this || !g_Game || !g_Game->IsInitialized()) {
        return nullptr;
    }

    const std::string className = Mapper::Get("net/minecraft/util/MovingObjectPosition");
    const std::string fieldName = Mapper::Get("entityHit");
    const std::string fieldSignature = Mapper::Get("net/minecraft/entity/Entity", 2);
    if (className.empty() || fieldName.empty() || fieldSignature.empty()) {
        return nullptr;
    }

    Class* movingObjectClass = g_Game->FindClass(className);
    Field* field = movingObjectClass ? movingObjectClass->GetField(env, fieldName.c_str(), fieldSignature.c_str()) : nullptr;
    return field ? field->GetObjectField(env, this) : nullptr;
}
