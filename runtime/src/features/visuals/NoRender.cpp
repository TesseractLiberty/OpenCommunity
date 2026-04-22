#include "pch.h"
#include "NoRender.h"

#include "../../game/classes/AxisAlignedBB.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/World.h"
#include "../../game/jni/Class.h"
#include "../../game/jni/Field.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/jni/Method.h"
#include "../../game/mapping/Mapper.h"

#include <cmath>

namespace {
    constexpr float kFallbackDistance = 1000.0f;

    void ClearJniException(JNIEnv* env) {
        if (env && env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    jclass GetMappedClass(const char* mappingKey) {
        if (!g_Game || !g_Game->IsInitialized() || !mappingKey) {
            return nullptr;
        }

        const std::string className = Mapper::Get(mappingKey);
        return className.empty() ? nullptr : reinterpret_cast<jclass>(g_Game->FindClass(className));
    }

    bool IsInstanceOfMapped(JNIEnv* env, jobject object, const char* mappingKey) {
        if (!env || !object) {
            return false;
        }

        jclass mappedClass = GetMappedClass(mappingKey);
        if (!mappedClass) {
            return false;
        }

        const bool value = env->IsInstanceOf(object, mappedClass) == JNI_TRUE;
        ClearJniException(env);
        return value;
    }

    jobject GetEntityBoundingBox(JNIEnv* env, jobject entity) {
        if (!env || !entity) {
            return nullptr;
        }

        auto* entityClass = reinterpret_cast<Class*>(env->GetObjectClass(entity));
        if (!entityClass) {
            return nullptr;
        }

        const std::string boundingBoxSignature = Mapper::Get("net/minecraft/util/AxisAlignedBB", 2);
        const std::string fieldName = Mapper::Get("boundingBox");
        jobject boundingBox = nullptr;

        if (!fieldName.empty() && !boundingBoxSignature.empty()) {
            Field* field = entityClass->GetField(env, fieldName.c_str(), boundingBoxSignature.c_str());
            boundingBox = field ? field->GetObjectField(env, entity) : nullptr;
        }

        if (!boundingBox) {
            const std::string methodName = Mapper::Get("getEntityBoundingBox");
            const std::string methodSignature = Mapper::Get("net/minecraft/util/AxisAlignedBB", 3);
            Method* method = (!methodName.empty() && !methodSignature.empty()) ? entityClass->GetMethod(env, methodName.c_str(), methodSignature.c_str()) : nullptr;
            boundingBox = method ? method->CallObjectMethod(env, entity) : nullptr;
        }

        env->DeleteLocalRef(reinterpret_cast<jclass>(entityClass));
        return boundingBox;
    }

    double GetRenderDistanceWeight(JNIEnv* env, jobject entity) {
        if (!env || !entity) {
            return 1.0;
        }

        auto* entityClass = reinterpret_cast<Class*>(env->GetObjectClass(entity));
        if (!entityClass) {
            return 1.0;
        }

        const std::string fieldName = Mapper::Get("renderDistanceWeight");
        Field* field = fieldName.empty() ? nullptr : entityClass->GetField(env, fieldName.c_str(), "D");
        const double value = field ? field->GetDoubleField(env, entity) : 1.0;
        env->DeleteLocalRef(reinterpret_cast<jclass>(entityClass));
        return value;
    }

    void SetRenderDistanceWeight(JNIEnv* env, jobject entity, double value) {
        if (!env || !entity) {
            return;
        }

        auto* entityClass = reinterpret_cast<Class*>(env->GetObjectClass(entity));
        if (!entityClass) {
            return;
        }

        const std::string fieldName = Mapper::Get("renderDistanceWeight");
        Field* field = fieldName.empty() ? nullptr : entityClass->GetField(env, fieldName.c_str(), "D");
        if (field) {
            field->SetDoubleField(env, entity, value);
        }

        env->DeleteLocalRef(reinterpret_cast<jclass>(entityClass));
    }

    float GetDistanceToEntityBox(JNIEnv* env, jobject localPlayer, jobject entity) {
        if (!env || !localPlayer || !entity) {
            return kFallbackDistance;
        }

        auto* player = reinterpret_cast<Player*>(localPlayer);
        jobject boundingBoxObject = GetEntityBoundingBox(env, entity);
        if (!boundingBoxObject) {
            return player->GetDistanceToEntity(entity, env);
        }

        const AxisAlignedBB_t box = reinterpret_cast<AxisAlignedBB*>(boundingBoxObject)->GetNativeBoundingBox(env);
        env->DeleteLocalRef(boundingBoxObject);

        const Vec3D position = player->GetPos(env);
        const double dx = (std::max)({ static_cast<double>(box.minX) - position.x, 0.0, position.x - static_cast<double>(box.maxX) });
        const double dy = (std::max)({ static_cast<double>(box.minY) - position.y, 0.0, position.y - static_cast<double>(box.maxY) });
        const double dz = (std::max)({ static_cast<double>(box.minZ) - position.z, 0.0, position.z - static_cast<double>(box.maxZ) });
        const double distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
        return std::isfinite(distance) ? static_cast<float>(distance) : kFallbackDistance;
    }

}

void NoRender::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (!env || env->PushLocalFrame(1024) != 0) {
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    jobject localPlayer = Minecraft::GetThePlayer(env);
    if (!worldObject || !localPlayer) {
        env->PopLocalFrame(nullptr);
        return;
    }

    if (m_LastWorld && !env->IsSameObject(m_LastWorld, worldObject)) {
        env->DeleteGlobalRef(m_LastWorld);
        m_LastWorld = nullptr;
        m_AppliedEntityOverrides = false;
    }

    if (!m_LastWorld) {
        m_LastWorld = env->NewGlobalRef(worldObject);
    }

    const bool enabled = IsEnabled();
    if (enabled != m_WasEnabled) {
        if (!enabled) {
            ResetEntityRendering(env, worldObject, localPlayer);
        }
        m_WasEnabled = enabled;
    }

    if (!enabled) {
        if (m_AppliedEntityOverrides) {
            ResetEntityRendering(env, worldObject, localPlayer);
        }
        env->PopLocalFrame(nullptr);
        return;
    }

    ApplyEntityRendering(env, worldObject, localPlayer);
    MarkInUse(120);

    env->PopLocalFrame(nullptr);
}

bool NoRender::ShouldStopRender(JNIEnv* env, jobject entity, jobject localPlayer) const {
    if (!env || !entity || !localPlayer || env->IsSameObject(entity, localPlayer)) {
        return false;
    }

    const bool selected =
        GetBoolOption(kAllEntitiesOption, true) ||
        (GetBoolOption(kItemsOption, true) && IsInstanceOfMapped(env, entity, "net/minecraft/entity/item/EntityItem")) ||
        (GetBoolOption(kPlayersOption, true) && IsInstanceOfMapped(env, entity, "net/minecraft/entity/player/EntityPlayer")) ||
        (GetBoolOption(kMobsOption, true) && IsInstanceOfMapped(env, entity, "net/minecraft/entity/monster/EntityMob")) ||
        (GetBoolOption(kAnimalsOption, true) && IsInstanceOfMapped(env, entity, "net/minecraft/entity/passive/EntityAnimal")) ||
        (GetBoolOption(kArmorStandOption, true) && IsInstanceOfMapped(env, entity, "net/minecraft/entity/item/EntityArmorStand"));

    return selected && GetDistanceToEntityBox(env, localPlayer, entity) > GetFloatOption(kMaxRenderRangeOption, 4.0f);
}

void NoRender::ApplyEntityRendering(JNIEnv* env, jobject worldObject, jobject localPlayer) {
    if (!env || !worldObject || !localPlayer) {
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    const auto entities = world->GetLoadedEntities(env);
    bool changedAny = false;

    for (jobject entity : entities) {
        if (!entity) {
            continue;
        }

        if (env->IsSameObject(entity, localPlayer)) {
            env->DeleteLocalRef(entity);
            continue;
        }

        if (ShouldStopRender(env, entity, localPlayer)) {
            SetRenderDistanceWeight(env, entity, 0.0);
            changedAny = true;
        } else if (GetBoolOption(kAutoResetOption, true)) {
            SetRenderDistanceWeight(env, entity, 1.0);
        }

        env->DeleteLocalRef(entity);
    }

    m_AppliedEntityOverrides = changedAny || m_AppliedEntityOverrides;
}

void NoRender::ResetEntityRendering(JNIEnv* env, jobject worldObject, jobject localPlayer) {
    if (!env || !worldObject || !localPlayer) {
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    const auto entities = world->GetLoadedEntities(env);

    for (jobject entity : entities) {
        if (!entity) {
            continue;
        }

        if (!env->IsSameObject(entity, localPlayer) && GetRenderDistanceWeight(env, entity) <= 0.0) {
            SetRenderDistanceWeight(env, entity, 1.0);
        }

        env->DeleteLocalRef(entity);
    }

    m_AppliedEntityOverrides = false;
}
