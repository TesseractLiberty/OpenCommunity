#include "pch.h"
#include "ItemChams.h"

#include "Notifications.h"

#include "../../core/RenderHook.h"
#include "../../game/classes/AxisAlignedBB.h"
#include "../../game/classes/EntityItem.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/RenderHelper.h"
#include "../../game/classes/RenderItem.h"
#include "../../game/classes/RenderManager.h"
#include "../../game/classes/Timer.h"
#include "../../game/classes/World.h"
#include "../../game/jni/Class.h"
#include "../../game/jni/Field.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/jni/Method.h"
#include "../../game/mapping/Mapper.h"

#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <gl/GL.h>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
    constexpr float kMinProjectedSize = 2.0f;
    constexpr float kMinIconSize = 12.0f;
    constexpr float kMaxIconSize = 24.0f;
    constexpr float kDrawWorldScale = 1.0f;
    constexpr double kDrawWorldYOffset = 0.0;
    constexpr double kDropMaxHorizontalDistanceSq = 16.0;
    constexpr double kDropMaxVerticalDistance = 2.6;
    constexpr double kPickupMaxHorizontalDistanceSq = 4.0;
    constexpr double kPickupMaxVerticalDistance = 2.8;
    constexpr double kTrackedItemMaxLocalDistanceSq = 196.0;
    constexpr auto kDropNotificationCooldown = std::chrono::milliseconds(750);
    constexpr auto kPickupNotificationCooldown = std::chrono::milliseconds(750);
    constexpr auto kDropNotificationCacheTtl = std::chrono::seconds(3);
    constexpr auto kPickupNotificationCacheTtl = std::chrono::seconds(3);
    constexpr auto kPickupRecentSeenWindow = std::chrono::milliseconds(900);

    struct ScreenPoint {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct WorldPoint {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct ClipPoint {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct ProjectedItemBox {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
    };

    struct RenderMatrixSnapshot {
        std::vector<float> modelView;
        std::vector<float> projection;
        int viewportWidth = 0;
        int viewportHeight = 0;

        bool IsValid() const {
            return modelView.size() == 16 &&
                projection.size() == 16 &&
                viewportWidth > 0 &&
                viewportHeight > 0;
        }
    };

    struct ItemRenderEntry {
        jobject entity = nullptr;
        jobject stack = nullptr;
        ProjectedItemBox projectedBox;
        ImU32 fillColor = IM_COL32_WHITE;
        ImU32 lineColor = IM_COL32_WHITE;
        ImVec2 iconPosition{};
        float iconScale = 1.0f;
        double renderX = 0.0;
        double renderY = 0.0;
        double renderZ = 0.0;
        float entityYaw = 0.0f;
    };

    struct DropCandidate {
        bool found = false;
        bool localPlayer = false;
        std::string playerName;
        double score = (std::numeric_limits<double>::max)();
        double horizontalDistanceSq = (std::numeric_limits<double>::max)();
    };

    void ClearJniException(JNIEnv* env) {
        if (env && env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    ClipPoint MultiplyMatrixVector(const ClipPoint& vec, const std::vector<float>& mat) {
        if (mat.size() != 16) {
            return {};
        }

        return {
            vec.x * mat[0] + vec.y * mat[4] + vec.z * mat[8] + vec.w * mat[12],
            vec.x * mat[1] + vec.y * mat[5] + vec.z * mat[9] + vec.w * mat[13],
            vec.x * mat[2] + vec.y * mat[6] + vec.z * mat[10] + vec.w * mat[14],
            vec.x * mat[3] + vec.y * mat[7] + vec.z * mat[11] + vec.w * mat[15]
        };
    }

    RenderMatrixSnapshot CaptureRenderMatrixSnapshot() {
        std::lock_guard<std::mutex> lock(RenderCache::mtx);
        return {
            RenderCache::modelView,
            RenderCache::projection,
            RenderCache::viewportW,
            RenderCache::viewportH
        };
    }

    bool TryProjectPoint(const WorldPoint& worldPoint, const RenderMatrixSnapshot& snapshot, ScreenPoint& screenPoint) {
        if (!snapshot.IsValid()) {
            return false;
        }

        const ClipPoint clipPoint = MultiplyMatrixVector(
            MultiplyMatrixVector({ worldPoint.x, worldPoint.y, worldPoint.z, 1.0f }, snapshot.modelView),
            snapshot.projection);
        if (!std::isfinite(clipPoint.x) || !std::isfinite(clipPoint.y) ||
            !std::isfinite(clipPoint.z) || !std::isfinite(clipPoint.w) ||
            std::fabs(clipPoint.w) < 0.0001f) {
            return false;
        }

        const float ndcX = clipPoint.x / clipPoint.w;
        const float ndcY = clipPoint.y / clipPoint.w;
        const float ndcZ = clipPoint.z / clipPoint.w;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || !std::isfinite(ndcZ) ||
            ndcZ < -1.0f || ndcZ > 1.0f) {
            return false;
        }

        screenPoint.x = snapshot.viewportWidth * ((ndcX + 1.0f) * 0.5f);
        screenPoint.y = snapshot.viewportHeight * ((1.0f - ndcY) * 0.5f);
        return std::isfinite(screenPoint.x) && std::isfinite(screenPoint.y);
    }

    bool TryBuildProjectedItemBox(const AxisAlignedBB_t& boundingBox, const RenderMatrixSnapshot& snapshot, ProjectedItemBox& projectedBox) {
        if (!snapshot.IsValid()) {
            return false;
        }

        const std::array<WorldPoint, 8> corners = {{
            { boundingBox.minX, boundingBox.minY, boundingBox.minZ },
            { boundingBox.maxX, boundingBox.minY, boundingBox.minZ },
            { boundingBox.maxX, boundingBox.minY, boundingBox.maxZ },
            { boundingBox.minX, boundingBox.minY, boundingBox.maxZ },
            { boundingBox.minX, boundingBox.maxY, boundingBox.minZ },
            { boundingBox.maxX, boundingBox.maxY, boundingBox.minZ },
            { boundingBox.maxX, boundingBox.maxY, boundingBox.maxZ },
            { boundingBox.minX, boundingBox.maxY, boundingBox.maxZ }
        }};

        int visibleCorners = 0;
        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;

        for (const auto& corner : corners) {
            ScreenPoint projectedCorner;
            if (!TryProjectPoint(corner, snapshot, projectedCorner)) {
                continue;
            }

            minX = (std::min)(minX, projectedCorner.x);
            minY = (std::min)(minY, projectedCorner.y);
            maxX = (std::max)(maxX, projectedCorner.x);
            maxY = (std::max)(maxY, projectedCorner.y);
            ++visibleCorners;
        }

        if (visibleCorners == 0 ||
            !std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY) ||
            (maxX - minX) < kMinProjectedSize ||
            (maxY - minY) < kMinProjectedSize) {
            return false;
        }

        projectedBox.minX = minX;
        projectedBox.minY = minY;
        projectedBox.maxX = maxX;
        projectedBox.maxY = maxY;
        return true;
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

    int GetObjectIdentityHash(JNIEnv* env, jobject object) {
        if (!env || !object) {
            return 0;
        }

        static jclass s_SystemClass = nullptr;
        static jmethodID s_IdentityHashCode = nullptr;
        if (!s_SystemClass || !s_IdentityHashCode) {
            jclass systemClass = env->FindClass("java/lang/System");
            if (!systemClass) {
                ClearJniException(env);
                return 0;
            }

            s_SystemClass = reinterpret_cast<jclass>(env->NewGlobalRef(systemClass));
            env->DeleteLocalRef(systemClass);
            if (!s_SystemClass) {
                return 0;
            }

            s_IdentityHashCode = env->GetStaticMethodID(s_SystemClass, "identityHashCode", "(Ljava/lang/Object;)I");
            if (!s_IdentityHashCode) {
                ClearJniException(env);
                return 0;
            }
        }

        const jint hash = env->CallStaticIntMethod(s_SystemClass, s_IdentityHashCode, object);
        ClearJniException(env);
        return static_cast<int>(hash);
    }

    double DistanceSq2D(double ax, double az, double bx, double bz) {
        const double dx = ax - bx;
        const double dz = az - bz;
        return (dx * dx) + (dz * dz);
    }

    double DistanceSq3D(const Vec3D& a, const Vec3D& b) {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        const double dz = a.z - b.z;
        return (dx * dx) + (dy * dy) + (dz * dz);
    }

    double PositiveDot(double value) {
        return value > 0.0 ? value : 0.0;
    }

    bool TryFindLikelyPlayerNearItem(
        JNIEnv* env,
        jobject worldObject,
        const Vec3D& itemPos,
        double maxHorizontalDistanceSq,
        double maxVerticalDistance,
        double facingWeight,
        double movementWeight,
        double closeRangeBonus,
        double closeRangeDistanceSq,
        double ambiguityDistanceSq,
        double ambiguityScoreDelta,
        DropCandidate& candidate) {
        candidate = {};

        if (!env || !worldObject) {
            return false;
        }

        jobject localPlayerObject = Minecraft::GetThePlayer(env);
        auto* world = reinterpret_cast<World*>(worldObject);
        const auto players = world->GetPlayerEntities(env);

        double secondBestScore = (std::numeric_limits<double>::max)();
        for (Player* player : players) {
            jobject playerObject = reinterpret_cast<jobject>(player);
            if (!playerObject) {
                continue;
            }

            const bool isLocalPlayer = localPlayerObject && env->IsSameObject(playerObject, localPlayerObject);

            const std::string playerName = player->GetName(env, true);
            const Vec3D playerPos = player->GetPos(env);
            const Vec3D lastTickPos = player->GetLastTickPos(env);
            const float playerYaw = player->GetRotationYaw(env);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(playerObject);
                continue;
            }

            const double verticalDistance = std::fabs(itemPos.y - playerPos.y);
            const double horizontalDistanceSq = DistanceSq2D(itemPos.x, itemPos.z, playerPos.x, playerPos.z);
            if (playerName.empty() ||
                verticalDistance > maxVerticalDistance ||
                horizontalDistanceSq > maxHorizontalDistanceSq) {
                env->DeleteLocalRef(playerObject);
                continue;
            }

            const double dropVectorX = itemPos.x - playerPos.x;
            const double dropVectorZ = itemPos.z - playerPos.z;
            const double dropDistance = std::sqrt(horizontalDistanceSq);

            double facingBonus = 0.0;
            if (dropDistance > 0.001) {
                const double yawRadians = static_cast<double>(playerYaw) * 3.14159265358979323846 / 180.0;
                const double forwardX = -std::sin(yawRadians);
                const double forwardZ = std::cos(yawRadians);
                const double normalizedDropX = dropVectorX / dropDistance;
                const double normalizedDropZ = dropVectorZ / dropDistance;
                facingBonus = PositiveDot((normalizedDropX * forwardX) + (normalizedDropZ * forwardZ));
            }

            const double movementX = playerPos.x - lastTickPos.x;
            const double movementZ = playerPos.z - lastTickPos.z;
            const double movementDistance = std::sqrt((movementX * movementX) + (movementZ * movementZ));
            double movementBonus = 0.0;
            if (movementDistance > 0.001 && dropDistance > 0.001) {
                movementBonus = PositiveDot(
                    ((dropVectorX / dropDistance) * (movementX / movementDistance)) +
                    ((dropVectorZ / dropDistance) * (movementZ / movementDistance)));
            }

            double score = horizontalDistanceSq + (verticalDistance * 1.35);
            score -= facingBonus * facingWeight;
            score -= movementBonus * movementWeight;
            if (horizontalDistanceSq <= closeRangeDistanceSq) {
                score -= closeRangeBonus;
            }

            if (score < candidate.score) {
                secondBestScore = candidate.score;
                candidate.found = true;
                candidate.localPlayer = isLocalPlayer;
                candidate.playerName = playerName;
                candidate.score = score;
                candidate.horizontalDistanceSq = horizontalDistanceSq;
            } else if (score < secondBestScore) {
                secondBestScore = score;
            }

            env->DeleteLocalRef(playerObject);
        }

        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }

        if (!candidate.found) {
            return false;
        }

        if (candidate.localPlayer) {
            return false;
        }

        if (candidate.horizontalDistanceSq > ambiguityDistanceSq &&
            secondBestScore < (std::numeric_limits<double>::max)() &&
            (secondBestScore - candidate.score) < ambiguityScoreDelta) {
            return false;
        }

        return true;
    }

    bool TryFindLikelyDropper(JNIEnv* env, jobject worldObject, jobject itemEntityObject, DropCandidate& candidate) {
        if (!env || !worldObject || !itemEntityObject) {
            candidate = {};
            return false;
        }

        const Vec3D itemPos = reinterpret_cast<Player*>(itemEntityObject)->GetPos(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            candidate = {};
            return false;
        }

        return TryFindLikelyPlayerNearItem(
            env,
            worldObject,
            itemPos,
            kDropMaxHorizontalDistanceSq,
            kDropMaxVerticalDistance,
            0.85,
            0.45,
            0.35,
            0.75,
            1.6,
            0.12,
            candidate);
    }

    bool TryFindLikelyCollector(JNIEnv* env, jobject worldObject, const Vec3D& itemPos, DropCandidate& candidate) {
        return TryFindLikelyPlayerNearItem(
            env,
            worldObject,
            itemPos,
            kPickupMaxHorizontalDistanceSq,
            kPickupMaxVerticalDistance,
            0.35,
            0.80,
            0.55,
            1.10,
            0.9,
            0.08,
            candidate);
    }

    double GetDistanceSqToLocalPlayer(JNIEnv* env, const Vec3D& itemPos) {
        if (!env) {
            return (std::numeric_limits<double>::max)();
        }

        jobject localPlayerObject = Minecraft::GetThePlayer(env);
        if (!localPlayerObject) {
            return (std::numeric_limits<double>::max)();
        }

        const Vec3D localPos = reinterpret_cast<Player*>(localPlayerObject)->GetPos(env);
        env->DeleteLocalRef(localPlayerObject);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return (std::numeric_limits<double>::max)();
        }

        return DistanceSq3D(itemPos, localPos);
    }

    bool TryGetArmorPercentage(JNIEnv* env, jobject stackObject, int& percentage) {
        percentage = 0;

        if (!env || !stackObject) {
            return false;
        }

        auto* stack = reinterpret_cast<ItemStack*>(stackObject);
        if (!stack->IsArmor(env)) {
            return false;
        }

        const int maxDamage = stack->GetMaxDamage(env);
        if (maxDamage <= 0) {
            return false;
        }

        const int remainingDurability = (std::max)(0, maxDamage - stack->GetItemDamage(env));
        percentage = static_cast<int>(std::round((static_cast<float>(remainingDurability) / static_cast<float>(maxDamage)) * 100.0f));
        percentage = (std::clamp)(percentage, 0, 100);
        return true;
    }

    ImU32 BuildDurabilityColor(int percentage, int alpha) {
        if (percentage >= 100) {
            return IM_COL32(93, 232, 255, alpha);
        }
        if (percentage >= 80) {
            return IM_COL32(102, 214, 112, alpha);
        }
        if (percentage >= 60) {
            return IM_COL32(255, 219, 97, alpha);
        }
        if (percentage >= 40) {
            return IM_COL32(255, 167, 72, alpha);
        }
        if (percentage >= 20) {
            return IM_COL32(255, 98, 98, alpha);
        }
        return IM_COL32(255, 98, 98, alpha);
    }

    ImU32 BuildFillColor(int percentage) {
        return BuildDurabilityColor(percentage, 72);
    }

    ImU32 BuildLineColor(int percentage) {
        return BuildDurabilityColor(percentage, 235);
    }

    jobject GetEntityRenderObject(JNIEnv* env, jobject renderManagerObject, jobject entityObject) {
        if (!env || !renderManagerObject || !entityObject || !g_Game || !g_Game->IsInitialized()) {
            return nullptr;
        }

        const std::string entitySignature = Mapper::Get("net/minecraft/entity/Entity", 2);
        const std::string renderSignature = Mapper::Get("net/minecraft/client/renderer/entity/Render", 2);
        const std::string methodName = Mapper::Get("getEntityRenderObject");
        if (entitySignature.empty() || renderSignature.empty() || methodName.empty()) {
            return nullptr;
        }

        auto* renderManagerClass = reinterpret_cast<Class*>(env->GetObjectClass(renderManagerObject));
        if (!renderManagerClass) {
            return nullptr;
        }

        Method* method = renderManagerClass->GetMethod(
            env,
            methodName.c_str(),
            ("(" + entitySignature + ")" + renderSignature).c_str());
        jobject renderObject = method ? method->CallObjectMethod(env, renderManagerObject, false, entityObject) : nullptr;
        env->DeleteLocalRef(reinterpret_cast<jclass>(renderManagerClass));
        return renderObject;
    }

    void RenderEntityWithGameRenderer(
        JNIEnv* env,
        jobject renderObject,
        jobject entityObject,
        double renderX,
        double renderY,
        double renderZ,
        float entityYaw,
        float partialTicks) {
        if (!env || !renderObject || !entityObject) {
            return;
        }

        const std::string entitySignature = Mapper::Get("net/minecraft/entity/Entity", 2);
        const std::string methodName = Mapper::Get("doRender");
        if (entitySignature.empty() || methodName.empty()) {
            return;
        }

        auto* renderClass = reinterpret_cast<Class*>(env->GetObjectClass(renderObject));
        if (!renderClass) {
            return;
        }

        Method* method = renderClass->GetMethod(
            env,
            methodName.c_str(),
            ("(" + entitySignature + "DDDFF)V").c_str());
        if (method) {
            method->CallVoidMethod(
                env,
                renderObject,
                false,
                entityObject,
                renderX,
                renderY,
                renderZ,
                entityYaw,
                partialTicks);
        }

        env->DeleteLocalRef(reinterpret_cast<jclass>(renderClass));
    }
}

void ItemChams::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (!env || env->PushLocalFrame(512) != 0) {
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        ReleaseWorldRef(env);
        ClearHiddenEntities();
        ClearItemTracking();
        m_WasEnabled = false;
        env->PopLocalFrame(nullptr);
        return;
    }

    if (m_LastWorld && !env->IsSameObject(m_LastWorld, worldObject)) {
        ReleaseWorldRef(env);
        ClearHiddenEntities();
        ClearItemTracking();
    }

    if (!m_LastWorld) {
        m_LastWorld = env->NewGlobalRef(worldObject);
    }

    const bool enabled = IsEnabled();
    if (!enabled) {
        if (m_WasEnabled) {
            RestoreHiddenArmorItems(env, worldObject);
        }

        ClearItemTracking();
        m_WasEnabled = false;
        env->PopLocalFrame(nullptr);
        return;
    }

    ApplyArmorFiltering(env, worldObject);
    if (IsDropNotificationsEnabled() || IsPickupNotificationsEnabled()) {
        UpdateItemNotifications(env, worldObject);
    } else {
        ClearItemTracking();
    }

    m_WasEnabled = true;
    MarkInUse(120);

    env->PopLocalFrame(nullptr);
}

void ItemChams::RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
    (void)screenW;
    (void)screenH;

    if (!IsEnabled() || !drawList || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    JNIEnv* env = g_Game->GetCurrentEnv();
    if (!env || env->PushLocalFrame(768) != 0) {
        return;
    }

    jobject currentScreenObject = Minecraft::GetCurrentScreen(env);
    if (currentScreenObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    const RenderMatrixSnapshot snapshot = CaptureRenderMatrixSnapshot();
    if (!snapshot.IsValid()) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    const auto entities = world->GetLoadedEntities(env);
    const int threshold = GetPercentageThreshold();
    const bool renderEsp = GetMode() == kModeEsp;
    std::vector<ItemRenderEntry> renderEntries;
    renderEntries.reserve(entities.size());

    float partialTicks = 0.0f;
    bool canRenderWorldDraw = false;

    if (!renderEsp) {
        jobject timerObject = Minecraft::GetTimer(env);

        if (timerObject) {
            partialTicks = reinterpret_cast<Timer*>(timerObject)->GetRenderPartialTicks(env);
            canRenderWorldDraw = !env->ExceptionCheck();
        }

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            canRenderWorldDraw = false;
        }

        if (timerObject) {
            env->DeleteLocalRef(timerObject);
        }
    }

    for (jobject entity : entities) {
        if (!entity) {
            continue;
        }

        if (!IsInstanceOfMapped(env, entity, "net/minecraft/entity/item/EntityItem")) {
            env->DeleteLocalRef(entity);
            continue;
        }

        jobject stackObject = reinterpret_cast<EntityItem*>(entity)->GetItemStack(env);
        int percentage = 0;
        if (!stackObject || !TryGetArmorPercentage(env, stackObject, percentage) || percentage < threshold) {
            if (stackObject) {
                env->DeleteLocalRef(stackObject);
            }
            env->DeleteLocalRef(entity);
            continue;
        }

        const int identityHash = GetObjectIdentityHash(env, entity);
        if (GetRenderDistanceWeight(env, entity) <= 0.0 && !WasHiddenByModule(identityHash)) {
            env->DeleteLocalRef(stackObject);
            env->DeleteLocalRef(entity);
            continue;
        }

        if (renderEsp) {
            jobject boundingBoxObject = GetEntityBoundingBox(env, entity);
            if (!boundingBoxObject) {
                env->DeleteLocalRef(stackObject);
                env->DeleteLocalRef(entity);
                continue;
            }

            const AxisAlignedBB_t boundingBox = reinterpret_cast<AxisAlignedBB*>(boundingBoxObject)->GetNativeBoundingBox(env);
            ProjectedItemBox projectedBox{};
            const bool projected = TryBuildProjectedItemBox(boundingBox, snapshot, projectedBox);
            env->DeleteLocalRef(boundingBoxObject);

            if (!projected) {
                env->DeleteLocalRef(stackObject);
                env->DeleteLocalRef(entity);
                continue;
            }

            const float boxWidth = projectedBox.maxX - projectedBox.minX;
            const float boxHeight = projectedBox.maxY - projectedBox.minY;
            const float iconSize = (std::clamp)((std::min)(boxWidth, boxHeight) * 0.9f, kMinIconSize, kMaxIconSize);
            const ImVec2 iconPosition(
                projectedBox.minX + ((boxWidth - iconSize) * 0.5f),
                projectedBox.minY + ((boxHeight - iconSize) * 0.5f));

            renderEntries.push_back({
                entity,
                stackObject,
                projectedBox,
                BuildFillColor(percentage),
                BuildLineColor(percentage),
                iconPosition,
                iconSize / 16.0f
            });
            continue;
        }

        if (!canRenderWorldDraw) {
            env->DeleteLocalRef(stackObject);
            env->DeleteLocalRef(entity);
            continue;
        }

        auto* entityBase = reinterpret_cast<Player*>(entity);
        const Vec3D currentPos = entityBase->GetPos(env);
        const Vec3D previousPos = entityBase->GetLastTickPos(env);
        const float previousYaw = entityBase->GetPrevRotationYaw(env);
        const float currentYaw = entityBase->GetRotationYaw(env);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(stackObject);
            env->DeleteLocalRef(entity);
            continue;
        }

        const double interpolatedX = previousPos.x + ((currentPos.x - previousPos.x) * partialTicks);
        const double interpolatedY = previousPos.y + ((currentPos.y - previousPos.y) * partialTicks);
        const double interpolatedZ = previousPos.z + ((currentPos.z - previousPos.z) * partialTicks);

        renderEntries.push_back({
            entity,
            stackObject,
            {},
            BuildFillColor(percentage),
            BuildLineColor(percentage),
            {},
            1.0f,
            interpolatedX,
            interpolatedY,
            interpolatedZ,
            previousYaw + ((currentYaw - previousYaw) * partialTicks)
        });
    }

    if (renderEsp) {
        for (const auto& entry : renderEntries) {
            const ImVec2 min(entry.projectedBox.minX, entry.projectedBox.minY);
            const ImVec2 max(entry.projectedBox.maxX, entry.projectedBox.maxY);
            drawList->AddRect(
                ImVec2(min.x - 2.0f, min.y - 2.0f),
                ImVec2(max.x + 2.0f, max.y + 2.0f),
                IM_COL32(0, 0, 0, 110),
                2.0f,
                0,
                3.0f);
            drawList->AddRectFilled(min, max, entry.fillColor, 2.0f);
            drawList->AddRect(min, max, entry.lineColor, 2.0f, 0, 1.6f);
        }
    }

    if (!renderEntries.empty() && renderEsp) {
        jobject renderItemObject = Minecraft::GetRenderItem(env);
        if (renderItemObject) {
            auto* renderItem = reinterpret_cast<RenderItem*>(renderItemObject);

            glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            GLint viewport[4] = {};
            glGetIntegerv(GL_VIEWPORT, viewport);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(0.0, viewport[2], viewport[3], 0.0, -1000.0, 1000.0);

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_ALWAYS);
            glEnable(GL_LIGHTING);
            glEnable(GL_COLOR_MATERIAL);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            RenderHelper::EnableGUIStandardItemLighting(env);

            for (const auto& entry : renderEntries) {
                glPushMatrix();
                glTranslatef(entry.iconPosition.x, entry.iconPosition.y, 0.0f);
                glScalef(entry.iconScale, entry.iconScale, 1.0f);
                renderItem->RenderItemIntoGUI(entry.stack, 0, 0, env);
                glPopMatrix();
            }

            RenderHelper::DisableStandardItemLighting(env);

            glDisable(GL_BLEND);
            glDisable(GL_COLOR_MATERIAL);
            glDisable(GL_LIGHTING);
            glDisable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);

            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();
            glPopAttrib();
        }
    }

    if (!renderEntries.empty() && !renderEsp) {
        jobject renderManagerObject = Minecraft::GetRenderManager(env);
        if (renderManagerObject) {
            glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_LIGHTING_BIT | GL_POLYGON_BIT | GL_TRANSFORM_BIT);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadMatrixf(snapshot.projection.data());

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadMatrixf(snapshot.modelView.data());

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, -1000000.0f);

            for (const auto& entry : renderEntries) {
                if (!entry.entity) {
                    continue;
                }

                jobject renderObject = GetEntityRenderObject(env, renderManagerObject, entry.entity);
                if (!renderObject) {
                    continue;
                }

                glPushMatrix();
                glTranslated(entry.renderX, entry.renderY + kDrawWorldYOffset, entry.renderZ);
                glScalef(kDrawWorldScale, kDrawWorldScale, kDrawWorldScale);
                RenderEntityWithGameRenderer(
                    env,
                    renderObject,
                    entry.entity,
                    0.0,
                    0.0,
                    0.0,
                    entry.entityYaw,
                    partialTicks);
                glPopMatrix();
                env->DeleteLocalRef(renderObject);
            }

            glPolygonOffset(1.0f, 1000000.0f);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();
            glPopAttrib();

            env->DeleteLocalRef(renderManagerObject);
        }
    }

    for (const auto& entry : renderEntries) {
        if (entry.entity) {
            env->DeleteLocalRef(entry.entity);
        }
        if (entry.stack) {
            env->DeleteLocalRef(entry.stack);
        }
    }

    env->PopLocalFrame(nullptr);
}

void ItemChams::ShutdownRuntime(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (!env || env->PushLocalFrame(128) != 0) {
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (worldObject) {
        RestoreHiddenArmorItems(env, worldObject);
    } else {
        ClearHiddenEntities();
    }

    ReleaseWorldRef(env);
    ClearItemTracking();
    m_WasEnabled = false;

    env->PopLocalFrame(nullptr);
}

void ItemChams::ApplyArmorFiltering(JNIEnv* env, jobject worldObject) {
    if (!env || !worldObject) {
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    const auto entities = world->GetLoadedEntities(env);
    const int threshold = GetPercentageThreshold();
    const bool drawMode = GetMode() == kModeDraw;

    for (jobject entity : entities) {
        if (!entity) {
            continue;
        }

        if (!IsInstanceOfMapped(env, entity, "net/minecraft/entity/item/EntityItem")) {
            env->DeleteLocalRef(entity);
            continue;
        }

        const int identityHash = GetObjectIdentityHash(env, entity);
        jobject stackObject = reinterpret_cast<EntityItem*>(entity)->GetItemStack(env);
        int percentage = 0;
        const bool isArmor = stackObject && TryGetArmorPercentage(env, stackObject, percentage);

        if (isArmor) {
            if (percentage < threshold || drawMode) {
                SetRenderDistanceWeight(env, entity, 0.0);
                RememberHiddenEntity(identityHash);
            } else if (ForgetHiddenEntity(identityHash)) {
                SetRenderDistanceWeight(env, entity, 1.0);
            }
        } else if (ForgetHiddenEntity(identityHash)) {
            SetRenderDistanceWeight(env, entity, 1.0);
        }

        if (stackObject) {
            env->DeleteLocalRef(stackObject);
        }
        env->DeleteLocalRef(entity);
    }
}

void ItemChams::UpdateItemNotifications(JNIEnv* env, jobject worldObject) {
    if (!env || !worldObject) {
        ClearItemTracking();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    auto* world = reinterpret_cast<World*>(worldObject);
    const auto entities = world->GetLoadedEntities(env);
    const int threshold = GetPercentageThreshold();
    const bool notifyDrops = IsDropNotificationsEnabled();
    const bool notifyPickups = IsPickupNotificationsEnabled();
    std::unordered_set<int> currentItemIds;
    currentItemIds.reserve(entities.size());

    for (jobject entity : entities) {
        if (!entity) {
            continue;
        }

        if (!IsInstanceOfMapped(env, entity, "net/minecraft/entity/item/EntityItem")) {
            env->DeleteLocalRef(entity);
            continue;
        }

        const int identityHash = GetObjectIdentityHash(env, entity);
        if (identityHash == 0) {
            env->DeleteLocalRef(entity);
            continue;
        }

        jobject stackObject = reinterpret_cast<EntityItem*>(entity)->GetItemStack(env);
        int percentage = 0;
        const bool eligibleArmor = stackObject &&
            TryGetArmorPercentage(env, stackObject, percentage) &&
            percentage >= threshold;
        std::string itemName = eligibleArmor ? reinterpret_cast<ItemStack*>(stackObject)->GetDisplayName(env) : "";
        if (stackObject) {
            env->DeleteLocalRef(stackObject);
        }

        if (!eligibleArmor) {
            env->DeleteLocalRef(entity);
            continue;
        }

        if (itemName.empty()) {
            itemName = "Unknown Item";
        }

        const Vec3D itemPos = reinterpret_cast<Player*>(entity)->GetPos(env);
        const double localDistanceSq = GetDistanceSqToLocalPlayer(env, itemPos);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(entity);
            continue;
        }

        currentItemIds.insert(identityHash);
        auto [trackedIt, inserted] = m_TrackedItems.try_emplace(identityHash);
        auto& tracked = trackedIt->second;
        if (inserted) {
            tracked.firstSeen = now;
        }
        tracked.lastSeen = now;
        tracked.lastX = itemPos.x;
        tracked.lastY = itemPos.y;
        tracked.lastZ = itemPos.z;
        tracked.lastLocalDistanceSq = localDistanceSq;
        tracked.percentage = percentage;
        tracked.itemName = itemName;

        if (notifyDrops && inserted && m_ItemTrackerWarmed) {
            DropCandidate candidate;
            if (TryFindLikelyDropper(env, worldObject, entity, candidate)) {
                const std::string cooldownKey = candidate.playerName + "\n" + itemName;
                const auto recentIt = m_RecentDropNotifications.find(cooldownKey);
                const bool canNotify = recentIt == m_RecentDropNotifications.end() ||
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - recentIt->second) >= kDropNotificationCooldown;

                if (canNotify) {
                    m_RecentDropNotifications[cooldownKey] = now;
                    const std::string message = "**" + candidate.playerName + "** est" "\xC3\xA1" " dropando **" + itemName + "**";
                    Notifications::SendInfo(message, "Item Drop");
                }
            }
        }

        env->DeleteLocalRef(entity);
    }

    if (notifyPickups && m_ItemTrackerWarmed) {
        for (auto it = m_TrackedItems.begin(); it != m_TrackedItems.end();) {
            if (currentItemIds.find(it->first) != currentItemIds.end()) {
                ++it;
                continue;
            }

            const TrackedItemState& tracked = it->second;
            const bool recentlySeen = (now - tracked.lastSeen) <= kPickupRecentSeenWindow;
            const bool stillMatchesThreshold = tracked.percentage >= threshold;
            const bool closeEnoughToLocal = tracked.lastLocalDistanceSq <= kTrackedItemMaxLocalDistanceSq;

            if (recentlySeen && stillMatchesThreshold && closeEnoughToLocal) {
                const Vec3D itemPos{ tracked.lastX, tracked.lastY, tracked.lastZ };
                DropCandidate candidate;
                if (TryFindLikelyCollector(env, worldObject, itemPos, candidate)) {
                    const std::string cooldownKey = candidate.playerName + "\n" + tracked.itemName;
                    const auto recentIt = m_RecentPickupNotifications.find(cooldownKey);
                    const bool canNotify = recentIt == m_RecentPickupNotifications.end() ||
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - recentIt->second) >= kPickupNotificationCooldown;

                    if (canNotify) {
                        m_RecentPickupNotifications[cooldownKey] = now;
                        const std::string message = "**" + candidate.playerName + "** pegou **" + tracked.itemName + "**";
                        Notifications::SendInfo(message, "Item Pickup");
                    }
                }
            }

            it = m_TrackedItems.erase(it);
        }
    } else {
        for (auto it = m_TrackedItems.begin(); it != m_TrackedItems.end();) {
            if (currentItemIds.find(it->first) == currentItemIds.end()) {
                it = m_TrackedItems.erase(it);
            } else {
                ++it;
            }
        }
    }

    m_ItemTrackerWarmed = true;

    for (auto it = m_RecentDropNotifications.begin(); it != m_RecentDropNotifications.end();) {
        if (now - it->second > kDropNotificationCacheTtl) {
            it = m_RecentDropNotifications.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_RecentPickupNotifications.begin(); it != m_RecentPickupNotifications.end();) {
        if (now - it->second > kPickupNotificationCacheTtl) {
            it = m_RecentPickupNotifications.erase(it);
        } else {
            ++it;
        }
    }
}

void ItemChams::RestoreHiddenArmorItems(JNIEnv* env, jobject worldObject) {
    if (!env || !worldObject) {
        ClearHiddenEntities();
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    const auto entities = world->GetLoadedEntities(env);

    for (jobject entity : entities) {
        if (!entity) {
            continue;
        }

        const int identityHash = GetObjectIdentityHash(env, entity);
        if (ForgetHiddenEntity(identityHash)) {
            SetRenderDistanceWeight(env, entity, 1.0);
        }

        env->DeleteLocalRef(entity);
    }

    ClearHiddenEntities();
}

void ItemChams::ReleaseWorldRef(JNIEnv* env) {
    if (env && m_LastWorld) {
        env->DeleteGlobalRef(m_LastWorld);
    }
    m_LastWorld = nullptr;
}

bool ItemChams::WasHiddenByModule(int identityHash) const {
    std::lock_guard<std::mutex> lock(m_HiddenMutex);
    return m_HiddenEntityIds.find(identityHash) != m_HiddenEntityIds.end();
}

void ItemChams::RememberHiddenEntity(int identityHash) {
    if (identityHash == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_HiddenMutex);
    m_HiddenEntityIds.insert(identityHash);
}

bool ItemChams::ForgetHiddenEntity(int identityHash) {
    if (identityHash == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_HiddenMutex);
    return m_HiddenEntityIds.erase(identityHash) > 0;
}

void ItemChams::ClearHiddenEntities() {
    std::lock_guard<std::mutex> lock(m_HiddenMutex);
    m_HiddenEntityIds.clear();
}

void ItemChams::ClearItemTracking() {
    m_TrackedItems.clear();
    m_RecentDropNotifications.clear();
    m_RecentPickupNotifications.clear();
    m_ItemTrackerWarmed = false;
}
