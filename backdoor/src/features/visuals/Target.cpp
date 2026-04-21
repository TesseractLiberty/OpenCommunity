#include "pch.h"
#include "Target.h"

#include "../../core/Bridge.h"
#include "../../core/RenderHook.h"
#include "../../game/classes/ItemArmor.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/MovingObjectPosition.h"
#include "../../game/classes/RenderManager.h"
#include "../../game/classes/Scoreboard.h"
#include "../../game/classes/Team.h"
#include "../../game/classes/Timer.h"
#include "../../game/classes/World.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/mapping/Mapper.h"

#include <array>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace {
    constexpr int kOnlineTargetPlayerLimit = 100;
    constexpr float kTargetEspLineThickness = 1.6f;
    constexpr float kTargetEspOutlineThickness = 3.4f;
    constexpr float kTargetHealthBarOffset = 8.0f;
    constexpr float kTargetHealthBarWidth = 6.0f;
    constexpr float kThirdPersonCameraDistance = 8.0f;
    constexpr float kRelativeProjectionDepthMax = 1.15f;
    constexpr float kThirdPersonRenderYOffset = 3.4f;

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

    struct ProjectedTargetBox {
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

    bool TryProjectPointInternal(
        const WorldPoint& worldPoint,
        const RenderMatrixSnapshot& snapshot,
        ScreenPoint& screenPoint,
        bool allowRelativeDepthRange) {
        if (!snapshot.IsValid()) {
            return false;
        }

        const ClipPoint clipPoint = MultiplyMatrixVector(
            MultiplyMatrixVector({ worldPoint.x, worldPoint.y, worldPoint.z, 1.0f }, snapshot.modelView),
            snapshot.projection);
        if (!std::isfinite(clipPoint.x) || !std::isfinite(clipPoint.y) ||
            !std::isfinite(clipPoint.z) || !std::isfinite(clipPoint.w)) {
            return false;
        }

        if (std::fabs(clipPoint.w) < 0.0001f) {
            return false;
        }

        const float ndcX = clipPoint.x / clipPoint.w;
        const float ndcY = clipPoint.y / clipPoint.w;
        const float ndcZ = clipPoint.z / clipPoint.w;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || !std::isfinite(ndcZ)) {
            return false;
        }

        const bool visibleDepth = allowRelativeDepthRange
            ? (ndcZ > 1.0f && ndcZ < kRelativeProjectionDepthMax)
            : (ndcZ >= -1.0f && ndcZ <= 1.0f);
        if (!visibleDepth) {
            return false;
        }

        screenPoint.x = snapshot.viewportWidth * ((ndcX + 1.0f) * 0.5f);
        screenPoint.y = snapshot.viewportHeight * ((1.0f - ndcY) * 0.5f);
        return std::isfinite(screenPoint.x) && std::isfinite(screenPoint.y);
    }

    bool TryProjectPoint(const WorldPoint& worldPoint, const RenderMatrixSnapshot& snapshot, ScreenPoint& screenPoint) {
        return TryProjectPointInternal(worldPoint, snapshot, screenPoint, false);
    }

    bool TryProjectRelativePoint(const WorldPoint& worldPoint, const RenderMatrixSnapshot& snapshot, ScreenPoint& screenPoint) {
        return TryProjectPointInternal(worldPoint, snapshot, screenPoint, true);
    }

    bool TryBuildProjectedTargetBox(
        double entityX,
        double entityY,
        double entityZ,
        float entityWidth,
        float entityHeight,
        const RenderMatrixSnapshot& snapshot,
        ProjectedTargetBox& projectedBox) {
        if (!snapshot.IsValid()) {
            return false;
        }

        if (!std::isfinite(entityX) || !std::isfinite(entityY) || !std::isfinite(entityZ) ||
            !std::isfinite(entityWidth) || !std::isfinite(entityHeight) ||
            entityWidth <= 0.05f || entityHeight <= 0.05f) {
            return false;
        }

        const float halfWidth = entityWidth * 0.5f;
        const std::array<WorldPoint, 8> corners = {{
            { static_cast<float>(entityX - halfWidth), static_cast<float>(entityY),                static_cast<float>(entityZ - halfWidth) },
            { static_cast<float>(entityX + halfWidth), static_cast<float>(entityY),                static_cast<float>(entityZ - halfWidth) },
            { static_cast<float>(entityX + halfWidth), static_cast<float>(entityY),                static_cast<float>(entityZ + halfWidth) },
            { static_cast<float>(entityX - halfWidth), static_cast<float>(entityY),                static_cast<float>(entityZ + halfWidth) },
            { static_cast<float>(entityX - halfWidth), static_cast<float>(entityY + entityHeight), static_cast<float>(entityZ - halfWidth) },
            { static_cast<float>(entityX + halfWidth), static_cast<float>(entityY + entityHeight), static_cast<float>(entityZ - halfWidth) },
            { static_cast<float>(entityX + halfWidth), static_cast<float>(entityY + entityHeight), static_cast<float>(entityZ + halfWidth) },
            { static_cast<float>(entityX - halfWidth), static_cast<float>(entityY + entityHeight), static_cast<float>(entityZ + halfWidth) }
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
            maxX <= minX || maxY <= minY) {
            return false;
        }

        projectedBox.minX = minX;
        projectedBox.minY = minY;
        projectedBox.maxX = maxX;
        projectedBox.maxY = maxY;
        return true;
    }

    bool TryComputeThirdPersonCameraPosition(
        JNIEnv* env,
        Player* localPlayer,
        RenderManager* renderManager,
        float partialTicks,
        int thirdPersonView,
        GameVersions gameVersion,
        Vec3D& cameraPos) {
        if (!env || !localPlayer || !renderManager || thirdPersonView == 0) {
            return false;
        }

        cameraPos = renderManager->GetRenderPos(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        if (gameVersion == GameVersions::LUNAR) {
            cameraPos.y += kThirdPersonRenderYOffset;
        }

        const float prevPitch = localPlayer->GetPrevRotationPitch(env);
        const float currentPitch = localPlayer->GetRotationPitch(env);
        const float prevYaw = localPlayer->GetPrevRotationYaw(env);
        const float currentYaw = localPlayer->GetRotationYaw(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }

        constexpr float kDegreesToRadians = 3.14159265358979f / 180.0f;
        const float pitch = prevPitch + (currentPitch - prevPitch) * partialTicks;
        const float yaw = prevYaw + (currentYaw - prevYaw) * partialTicks;
        float distance = kThirdPersonCameraDistance;
        if (thirdPersonView == 2) {
            distance = -distance;
        }

        const float cosYaw = cosf((yaw + 90.0f) * kDegreesToRadians);
        const float sinYaw = sinf((yaw + 90.0f) * kDegreesToRadians);
        const float cosPitch = cosf(pitch * kDegreesToRadians);
        const float sinPitch = sinf(pitch * kDegreesToRadians);

        cameraPos.x -= static_cast<double>(cosYaw * distance * cosPitch);
        cameraPos.y += static_cast<double>(distance * sinPitch);
        cameraPos.z -= static_cast<double>(sinYaw * distance * cosPitch);

        return std::isfinite(cameraPos.x) &&
            std::isfinite(cameraPos.y) &&
            std::isfinite(cameraPos.z);
    }

    bool TryBuildProjectedThirdPersonTargetBox(
        const Vec3D& cameraPos,
        const Vec3D& entityPos,
        const Vec3D& entityLastPos,
        float entityWidth,
        float entityHeight,
        float partialTicks,
        const RenderMatrixSnapshot& snapshot,
        ProjectedTargetBox& projectedBox) {
        if (!snapshot.IsValid()) {
            return false;
        }

        if (!std::isfinite(cameraPos.x) || !std::isfinite(cameraPos.y) || !std::isfinite(cameraPos.z) ||
            !std::isfinite(entityPos.x) || !std::isfinite(entityPos.y) || !std::isfinite(entityPos.z) ||
            !std::isfinite(entityLastPos.x) || !std::isfinite(entityLastPos.y) || !std::isfinite(entityLastPos.z) ||
            !std::isfinite(entityWidth) || !std::isfinite(entityHeight)) {
            return false;
        }

        const float boxWidth = (std::max)(0.7f, entityWidth);
        const float boxMidHeight = (std::max)(0.9f, entityHeight * 0.5f + 0.2f);
        const float diagonalWidth = boxWidth / 1.388888f;

        const auto buildRelativePoint = [&](float offsetX, float offsetY, float offsetZ) -> WorldPoint {
            return {
                static_cast<float>((cameraPos.x - offsetX) - entityLastPos.x + (entityLastPos.x - entityPos.x) * partialTicks),
                static_cast<float>((cameraPos.y - offsetY) - entityLastPos.y + (entityLastPos.y - entityPos.y) * partialTicks),
                static_cast<float>((cameraPos.z - offsetZ) - entityLastPos.z + (entityLastPos.z - entityPos.z) * partialTicks)
            };
        };

        const std::array<WorldPoint, 10> points = {{
            buildRelativePoint(0.0f, 0.0f, 0.0f),
            buildRelativePoint(0.0f, boxMidHeight * 2.0f, 0.0f),
            buildRelativePoint(boxWidth, boxMidHeight, 0.0f),
            buildRelativePoint(-boxWidth, boxMidHeight, 0.0f),
            buildRelativePoint(0.0f, boxMidHeight, boxWidth),
            buildRelativePoint(0.0f, boxMidHeight, -boxWidth),
            buildRelativePoint(diagonalWidth, boxMidHeight, diagonalWidth),
            buildRelativePoint(-diagonalWidth, boxMidHeight, -diagonalWidth),
            buildRelativePoint(-diagonalWidth, boxMidHeight, diagonalWidth),
            buildRelativePoint(diagonalWidth, boxMidHeight, -diagonalWidth)
        }};

        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;

        for (const auto& point : points) {
            ScreenPoint projectedPoint;
            if (!TryProjectRelativePoint(point, snapshot, projectedPoint)) {
                return false;
            }

            minX = (std::min)(minX, projectedPoint.x);
            minY = (std::min)(minY, projectedPoint.y);
            maxX = (std::max)(maxX, projectedPoint.x);
            maxY = (std::max)(maxY, projectedPoint.y);
        }

        if (!std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY) ||
            maxX <= minX || maxY <= minY) {
            return false;
        }

        projectedBox.minX = minX;
        projectedBox.minY = minY;
        projectedBox.maxX = maxX;
        projectedBox.maxY = maxY;
        return true;
    }

    float ComputeTargetHealthRatio(float currentHealth, float maxHealth) {
        const float safeHealth = (std::max)(0.0f, currentHealth);
        float safeMaxHealth = maxHealth;
        if (safeMaxHealth <= 0.0f) {
            safeMaxHealth = (std::max)(20.0f, safeHealth);
        }
        if (safeHealth > safeMaxHealth) {
            safeMaxHealth = safeHealth;
        }

        return (std::max)(0.0f, (std::min)(1.0f, safeHealth / safeMaxHealth));
    }

    std::string NormalizeTargetName(std::string name) {
        const auto notSpace = [](unsigned char ch) {
            return !std::isspace(ch);
        };

        const auto begin = std::find_if(name.begin(), name.end(), notSpace);
        if (begin == name.end()) {
            return {};
        }

        const auto end = std::find_if(name.rbegin(), name.rend(), notSpace).base();
        std::string normalized(begin, end);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return normalized;
    }

    bool TargetNamesMatch(const std::string& left, const std::string& right) {
        if (left.empty() || right.empty()) {
            return false;
        }

        return NormalizeTargetName(left) == NormalizeTargetName(right);
    }

    ImU32 BuildTargetEspColor(float currentHealth, float maxHealth, int hurtTime) {
        const float ratio = ComputeTargetHealthRatio(currentHealth, maxHealth);
        float red = 1.0f;
        float green = ratio > 0.5f ? 1.0f : ratio * 2.0f;
        if (ratio > 0.5f) {
            red = 1.0f - ((ratio - 0.5f) * 2.0f);
        }

        if (hurtTime > 0) {
            red = (std::min)(1.0f, red + 0.35f);
            green *= 0.55f;
        }

        return IM_COL32(
            static_cast<int>(red * 255.0f),
            static_cast<int>(green * 255.0f),
            0,
            255);
    }

    void DrawProjectedTargetBox(ImDrawList* drawList, const ProjectedTargetBox& projectedBox, ImU32 color) {
        if (!drawList) {
            return;
        }

        drawList->AddRect(
            ImVec2(projectedBox.minX, projectedBox.minY),
            ImVec2(projectedBox.maxX, projectedBox.maxY),
            IM_COL32(0, 0, 0, 190),
            0.0f,
            0,
            kTargetEspOutlineThickness);
        drawList->AddRect(
            ImVec2(projectedBox.minX, projectedBox.minY),
            ImVec2(projectedBox.maxX, projectedBox.maxY),
            color,
            0.0f,
            0,
            kTargetEspLineThickness);
    }

    void DrawTargetHealthBar(ImDrawList* drawList, const ProjectedTargetBox& projectedBox, float currentHealth, float maxHealth, ImU32 color) {
        if (!drawList) {
            return;
        }

        const float ratio = ComputeTargetHealthRatio(currentHealth, maxHealth);
        const float minX = projectedBox.minX - kTargetHealthBarOffset;
        const float maxX = minX + kTargetHealthBarWidth;
        const float minY = projectedBox.minY;
        const float maxY = projectedBox.maxY;
        const float barHeight = maxY - minY;
        if (!std::isfinite(barHeight) || barHeight <= 1.0f) {
            return;
        }

        drawList->AddRectFilled(
            ImVec2(minX, minY),
            ImVec2(maxX, maxY),
            IM_COL32(10, 10, 10, 200),
            0.0f);

        const float fillMinX = minX + 1.0f;
        const float fillMaxX = maxX - 1.0f;
        const float fillMaxY = maxY;
        const float fillMinY = maxY - (barHeight * ratio);
        drawList->AddRectFilled(
            ImVec2(fillMinX, fillMinY),
            ImVec2(fillMaxX, fillMaxY),
            color,
            0.0f);
    }

    bool ShouldSkipTargetEspForBadlionThirdPerson(JNIEnv* env) {
        if (!env || !g_Game || !g_Game->IsInitialized() ||
            g_Game->GetGameVersion() != GameVersions::BADLION) {
            return false;
        }

        const bool shouldSkip = Minecraft::GetThirdPersonView(env) != 0;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return shouldSkip;
    }

    struct BrowseCacheState {
        bool active = false;
        std::string clanTagDisplay;
        std::string normalizedClanTag;
        std::string registeredTeamName;
        std::unordered_set<std::string> members;
        std::unordered_set<std::string> processed;
    };

    std::mutex g_CurrentTargetMutex;
    std::string g_CurrentTargetName;
    std::atomic<bool> g_TargetActiveManages{ false };
    std::mutex g_TargetHealthMutex;
    std::unordered_map<std::string, float> g_TargetRealHealthCache;

    std::mutex g_BrowseMutex;
    BrowseCacheState g_BrowseCache;

    std::string StripMinecraftFormatting(std::string text) {
        std::string clean;
        clean.reserve(text.size());

        for (size_t index = 0; index < text.size(); ++index) {
            const unsigned char current = static_cast<unsigned char>(text[index]);
            if (current == 0xA7 || current == 0xC2) {
                if (current == 0xC2 && index + 1 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xA7) {
                    ++index;
                }
                if (index + 1 < text.size()) {
                    ++index;
                }
                continue;
            }

            clean.push_back(text[index]);
        }

        return clean;
    }

    std::string NormalizeBrowseTag(const std::string& tag) {
        std::string normalized;
        normalized.reserve(tag.size());
        for (const unsigned char ch : tag) {
            if (std::isalnum(ch)) {
                normalized.push_back(static_cast<char>(std::tolower(ch)));
            }
        }
        return normalized.size() >= 2 ? normalized : std::string{};
    }

    bool IsAllyColorName(const std::string& formattedName) {
        if (formattedName.empty()) {
            return false;
        }

        const bool hasGreen = formattedName.find("\247a") != std::string::npos || formattedName.find("\2472") != std::string::npos;
        const bool hasRed = formattedName.find("\247c") != std::string::npos || formattedName.find("\2474") != std::string::npos;
        return hasGreen && !hasRed;
    }

    void SyncBrowseCacheToConfig(const BrowseCacheState& snapshot) {
        auto* config = Bridge::Get()->GetConfig();
        if (!config) {
            return;
        }

        config->Target.m_BrowsedPlayersCount = 0;
        memset(config->Target.m_BrowsedPlayersProcessed, 0, sizeof(config->Target.m_BrowsedPlayersProcessed));
        memset(config->Target.m_BrowsedPlayerNames, 0, sizeof(config->Target.m_BrowsedPlayerNames));

        if (!snapshot.active || snapshot.members.empty()) {
            return;
        }

        std::vector<std::string> orderedMembers(snapshot.members.begin(), snapshot.members.end());
        std::sort(orderedMembers.begin(), orderedMembers.end());

        int count = 0;
        for (const auto& playerName : orderedMembers) {
            if (count >= 50) {
                break;
            }

            strncpy_s(config->Target.m_BrowsedPlayerNames[count], playerName.c_str(), _TRUNCATE);
            config->Target.m_BrowsedPlayersProcessed[count] = snapshot.processed.count(playerName) > 0;
            ++count;
        }

        config->Target.m_BrowsedPlayersCount = count;
    }

    void SyncBrowseCacheToConfig() {
        BrowseCacheState snapshot;
        {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            snapshot = g_BrowseCache;
        }
        SyncBrowseCacheToConfig(snapshot);
    }

    void ClearTargetHealthCache() {
        std::lock_guard<std::mutex> lock(g_TargetHealthMutex);
        g_TargetRealHealthCache.clear();
    }

    void UpdateTargetRealHealth(const std::string& playerName, float realHealth) {
        if (playerName.empty() || realHealth < 0.0f) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_TargetHealthMutex);
        g_TargetRealHealthCache[playerName] = realHealth;
    }

    float GetCachedTargetRealHealth(const std::string& playerName) {
        if (playerName.empty()) {
            return -1.0f;
        }

        std::lock_guard<std::mutex> lock(g_TargetHealthMutex);
        const auto it = g_TargetRealHealthCache.find(playerName);
        return it != g_TargetRealHealthCache.end() ? it->second : -1.0f;
    }

    void SyncTargetHealthCache(JNIEnv* env, World* world, jobject localPlayerObject) {
        if (!env || !world) {
            return;
        }

        const auto players = world->GetPlayerEntities(env);
        for (auto* player : players) {
            if (!player) {
                continue;
            }

            if (localPlayerObject && env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                continue;
            }

            const std::string playerName = player->GetName(env, true);
            const float realHealth = player->GetRealHealth(env);
            UpdateTargetRealHealth(playerName, realHealth);
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }
    }

    void MarkBrowsePlayerProcessed(const std::string& playerName) {
        if (playerName.empty()) {
            return;
        }

        BrowseCacheState snapshot;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            if (!g_BrowseCache.active || g_BrowseCache.members.count(playerName) == 0) {
                return;
            }

            changed = g_BrowseCache.processed.insert(playerName).second;
            snapshot = g_BrowseCache;
        }

        if (changed) {
            SyncBrowseCacheToConfig(snapshot);
        }
    }

    bool CaseInsensitiveNameLess(const std::string& lhs, const std::string& rhs) {
        return std::lexicographical_compare(
            lhs.begin(),
            lhs.end(),
            rhs.begin(),
            rhs.end(),
            [](unsigned char left, unsigned char right) {
                return std::tolower(left) < std::tolower(right);
            });
    }

    std::string ReadJavaString(JNIEnv* env, jstring stringObject) {
        if (!env || !stringObject) {
            return {};
        }

        const char* chars = env->GetStringUTFChars(stringObject, nullptr);
        std::string result = chars ? chars : "";
        if (chars) {
            env->ReleaseStringUTFChars(stringObject, chars);
        }
        return result;
    }

    jmethodID GetMappedMethodId(JNIEnv* env, jclass ownerClass, const char* mappingKey, const char* signature) {
        if (!env || !ownerClass || !mappingKey || !signature) {
            return nullptr;
        }

        const std::string methodName = Mapper::Get(mappingKey);
        if (methodName.empty()) {
            return nullptr;
        }

        jmethodID methodId = env->GetMethodID(ownerClass, methodName.c_str(), signature);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }

        return methodId;
    }

    void ClearOnlinePlayersToConfig(ModuleConfig* config) {
        if (!config) {
            return;
        }

        config->Target.m_OnlinePlayersCount = 0;
        memset(config->Target.m_OnlinePlayerNames, 0, sizeof(config->Target.m_OnlinePlayerNames));
    }

    bool CollectOnlinePlayersFromNetHandler(JNIEnv* env, jobject localPlayerObject, std::vector<std::string>& onlinePlayers) {
        if (!env) {
            return false;
        }

        jobject netHandlerObject = Minecraft::GetNetHandler(env);
        if (!netHandlerObject) {
            return false;
        }

        std::string localPlayerName;
        if (localPlayerObject) {
            localPlayerName = reinterpret_cast<Player*>(localPlayerObject)->GetName(env, true);
        }

        bool collectedAny = false;
        jclass netHandlerClass = env->GetObjectClass(netHandlerObject);
        jmethodID getPlayerInfoMapMethod = GetMappedMethodId(env, netHandlerClass, "getPlayerInfoMap", "()Ljava/util/Collection;");

        jobject playerInfoCollection = getPlayerInfoMapMethod
            ? env->CallObjectMethod(netHandlerObject, getPlayerInfoMapMethod)
            : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            playerInfoCollection = nullptr;
        }

        if (playerInfoCollection) {
            jclass collectionClass = env->GetObjectClass(playerInfoCollection);
            jmethodID toArrayMethod = collectionClass
                ? env->GetMethodID(collectionClass, "toArray", "()[Ljava/lang/Object;")
                : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                toArrayMethod = nullptr;
            }

            jobjectArray playerInfoArray = toArrayMethod
                ? static_cast<jobjectArray>(env->CallObjectMethod(playerInfoCollection, toArrayMethod))
                : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                playerInfoArray = nullptr;
            }

            if (playerInfoArray) {
                const jsize playerCount = env->GetArrayLength(playerInfoArray);
                for (jsize index = 0; index < playerCount; ++index) {
                    jobject playerInfoObject = env->GetObjectArrayElement(playerInfoArray, index);
                    if (!playerInfoObject) {
                        continue;
                    }

                    jclass playerInfoClass = env->GetObjectClass(playerInfoObject);
                    jmethodID getGameProfileMethod = GetMappedMethodId(
                        env,
                        playerInfoClass,
                        "getNetworkPlayerInfoGameProfile",
                        "()Lcom/mojang/authlib/GameProfile;");

                    jobject gameProfileObject = getGameProfileMethod
                        ? env->CallObjectMethod(playerInfoObject, getGameProfileMethod)
                        : nullptr;
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        gameProfileObject = nullptr;
                    }

                    if (gameProfileObject) {
                        jclass gameProfileClass = env->GetObjectClass(gameProfileObject);
                        jmethodID getNameMethod = gameProfileClass
                            ? env->GetMethodID(gameProfileClass, "getName", "()Ljava/lang/String;")
                            : nullptr;
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            getNameMethod = nullptr;
                        }

                        jstring playerNameObject = getNameMethod
                            ? static_cast<jstring>(env->CallObjectMethod(gameProfileObject, getNameMethod))
                            : nullptr;
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            playerNameObject = nullptr;
                        }

                        const std::string playerName = ReadJavaString(env, playerNameObject);
                        if (playerNameObject) {
                            env->DeleteLocalRef(playerNameObject);
                        }
                        if (gameProfileClass) {
                            env->DeleteLocalRef(gameProfileClass);
                        }
                        env->DeleteLocalRef(gameProfileObject);

                        if (!playerName.empty() && playerName != localPlayerName) {
                            onlinePlayers.push_back(playerName);
                            collectedAny = true;
                        }
                    }

                    if (playerInfoClass) {
                        env->DeleteLocalRef(playerInfoClass);
                    }
                    env->DeleteLocalRef(playerInfoObject);
                }

                env->DeleteLocalRef(playerInfoArray);
            }

            if (collectionClass) {
                env->DeleteLocalRef(collectionClass);
            }
            env->DeleteLocalRef(playerInfoCollection);
        }

        if (netHandlerClass) {
            env->DeleteLocalRef(netHandlerClass);
        }
        env->DeleteLocalRef(netHandlerObject);
        return collectedAny;
    }

    std::string GetNetworkPlayerInfoName(JNIEnv* env, jobject playerInfoObject, jclass playerInfoClass) {
        if (!env || !playerInfoObject || !playerInfoClass) {
            return {};
        }

        jmethodID getGameProfileMethod = GetMappedMethodId(
            env,
            playerInfoClass,
            "getNetworkPlayerInfoGameProfile",
            "()Lcom/mojang/authlib/GameProfile;");

        jobject gameProfileObject = getGameProfileMethod
            ? env->CallObjectMethod(playerInfoObject, getGameProfileMethod)
            : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            gameProfileObject = nullptr;
        }
        if (!gameProfileObject) {
            return {};
        }

        jclass gameProfileClass = env->GetObjectClass(gameProfileObject);
        jmethodID getNameMethod = gameProfileClass
            ? env->GetMethodID(gameProfileClass, "getName", "()Ljava/lang/String;")
            : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            getNameMethod = nullptr;
        }

        jstring playerNameObject = getNameMethod
            ? static_cast<jstring>(env->CallObjectMethod(gameProfileObject, getNameMethod))
            : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            playerNameObject = nullptr;
        }

        const std::string playerName = ReadJavaString(env, playerNameObject);
        if (playerNameObject) {
            env->DeleteLocalRef(playerNameObject);
        }
        if (gameProfileClass) {
            env->DeleteLocalRef(gameProfileClass);
        }
        env->DeleteLocalRef(gameProfileObject);
        return playerName;
    }

    bool IsSurvivalNetworkPlayer(JNIEnv* env, jobject playerInfoObject, jclass playerInfoClass) {
        if (!env || !playerInfoObject || !playerInfoClass) {
            return false;
        }

        const std::string gameTypeSignature = Mapper::Get("net/minecraft/world/WorldSettings$GameType", 2);
        if (gameTypeSignature.empty()) {
            return false;
        }

        jmethodID getGameTypeMethod = GetMappedMethodId(
            env,
            playerInfoClass,
            "getNetworkPlayerInfoGameType",
            ("()" + gameTypeSignature).c_str());
        jobject gameTypeObject = getGameTypeMethod
            ? env->CallObjectMethod(playerInfoObject, getGameTypeMethod)
            : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            gameTypeObject = nullptr;
        }
        if (!gameTypeObject) {
            return false;
        }

        jclass gameTypeClass = env->GetObjectClass(gameTypeObject);
        jmethodID getIdMethod = gameTypeClass
            ? GetMappedMethodId(env, gameTypeClass, "getGameTypeId", "()I")
            : nullptr;
        const int gameTypeId = getIdMethod ? env->CallIntMethod(gameTypeObject, getIdMethod) : -1;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        if (gameTypeClass) {
            env->DeleteLocalRef(gameTypeClass);
        }
        env->DeleteLocalRef(gameTypeObject);
        return gameTypeId == 0;
    }

    bool BrowseTeamMatches(JNIEnv* env, Team* team, const BrowseCacheState& cache) {
        if (!env || !team || !cache.active) {
            return false;
        }

        const std::string registeredName = team->GetRegisteredName(env);
        if (!registeredName.empty() && !cache.registeredTeamName.empty() && registeredName == cache.registeredTeamName) {
            return true;
        }

        if (cache.normalizedClanTag.empty()) {
            return false;
        }

        const std::string normalizedPrefix = NormalizeBrowseTag(StripMinecraftFormatting(team->GetColorPrefix(env)));
        const std::string normalizedSuffix = NormalizeBrowseTag(StripMinecraftFormatting(team->GetColorSuffix(env)));
        return (!normalizedPrefix.empty() && normalizedPrefix.find(cache.normalizedClanTag) != std::string::npos) ||
            (!normalizedSuffix.empty() && normalizedSuffix.find(cache.normalizedClanTag) != std::string::npos);
    }

    void CollectBrowseMembersFromNetHandler(JNIEnv* env, const BrowseCacheState& cache, std::unordered_set<std::string>& members) {
        if (!env || !cache.active) {
            return;
        }

        jobject netHandlerObject = Minecraft::GetNetHandler(env);
        if (!netHandlerObject) {
            return;
        }

        jclass netHandlerClass = env->GetObjectClass(netHandlerObject);
        jmethodID getPlayerInfoMapMethod = GetMappedMethodId(env, netHandlerClass, "getPlayerInfoMap", "()Ljava/util/Collection;");
        jobject playerInfoCollection = getPlayerInfoMapMethod
            ? env->CallObjectMethod(netHandlerObject, getPlayerInfoMapMethod)
            : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            playerInfoCollection = nullptr;
        }

        if (playerInfoCollection) {
            jclass collectionClass = env->GetObjectClass(playerInfoCollection);
            jmethodID toArrayMethod = collectionClass
                ? env->GetMethodID(collectionClass, "toArray", "()[Ljava/lang/Object;")
                : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                toArrayMethod = nullptr;
            }

            jobjectArray playerInfoArray = toArrayMethod
                ? static_cast<jobjectArray>(env->CallObjectMethod(playerInfoCollection, toArrayMethod))
                : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                playerInfoArray = nullptr;
            }

            if (playerInfoArray) {
                const std::string teamSignature = Mapper::Get("net/minecraft/scoreboard/ScorePlayerTeam", 2);
                const jsize playerCount = env->GetArrayLength(playerInfoArray);
                for (jsize index = 0; index < playerCount; ++index) {
                    jobject playerInfoObject = env->GetObjectArrayElement(playerInfoArray, index);
                    if (!playerInfoObject) {
                        continue;
                    }

                    jclass playerInfoClass = env->GetObjectClass(playerInfoObject);
                    if (!playerInfoClass || !IsSurvivalNetworkPlayer(env, playerInfoObject, playerInfoClass)) {
                        if (playerInfoClass) {
                            env->DeleteLocalRef(playerInfoClass);
                        }
                        env->DeleteLocalRef(playerInfoObject);
                        continue;
                    }

                    const std::string playerName = GetNetworkPlayerInfoName(env, playerInfoObject, playerInfoClass);
                    jobject playerTeamObject = nullptr;
                    if (!teamSignature.empty()) {
                        jmethodID getPlayerTeamMethod = GetMappedMethodId(
                            env,
                            playerInfoClass,
                            "getNetworkPlayerInfoPlayerTeam",
                            ("()" + teamSignature).c_str());
                        playerTeamObject = getPlayerTeamMethod
                            ? env->CallObjectMethod(playerInfoObject, getPlayerTeamMethod)
                            : nullptr;
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            playerTeamObject = nullptr;
                        }
                    }

                    if (!playerName.empty() && playerTeamObject &&
                        BrowseTeamMatches(env, reinterpret_cast<Team*>(playerTeamObject), cache)) {
                        members.insert(playerName);
                    }

                    if (playerTeamObject) {
                        env->DeleteLocalRef(playerTeamObject);
                    }
                    if (playerInfoClass) {
                        env->DeleteLocalRef(playerInfoClass);
                    }
                    env->DeleteLocalRef(playerInfoObject);
                }

                env->DeleteLocalRef(playerInfoArray);
            }

            if (collectionClass) {
                env->DeleteLocalRef(collectionClass);
            }
            env->DeleteLocalRef(playerInfoCollection);
        }

        if (netHandlerClass) {
            env->DeleteLocalRef(netHandlerClass);
        }
        env->DeleteLocalRef(netHandlerObject);
    }

    void SyncOnlinePlayersToConfig(JNIEnv* env, World* world, jobject localPlayerObject, ModuleConfig* config) {
        if (!config) {
            return;
        }

        static auto lastSyncTime = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (world && lastSyncTime.time_since_epoch().count() != 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSyncTime).count() < 200) {
            return;
        }
        lastSyncTime = now;

        ClearOnlinePlayersToConfig(config);
        if (!env || !world) {
            return;
        }

        std::vector<std::string> onlinePlayers;
        onlinePlayers.reserve(kOnlineTargetPlayerLimit);

        if (!CollectOnlinePlayersFromNetHandler(env, localPlayerObject, onlinePlayers)) {
            const auto players = world->GetPlayerEntities(env);
            for (auto* player : players) {
                if (!player) {
                    continue;
                }

                if (localPlayerObject && env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
                    env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                    continue;
                }

                const std::string playerName = player->GetName(env, true);
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));

                if (playerName.empty()) {
                    continue;
                }

                onlinePlayers.push_back(playerName);
            }
        }

        std::sort(onlinePlayers.begin(), onlinePlayers.end(), CaseInsensitiveNameLess);
        onlinePlayers.erase(std::unique(onlinePlayers.begin(), onlinePlayers.end(), [](const std::string& lhs, const std::string& rhs) {
            return _stricmp(lhs.c_str(), rhs.c_str()) == 0;
        }), onlinePlayers.end());

        int count = 0;
        for (const auto& playerName : onlinePlayers) {
            if (count >= kOnlineTargetPlayerLimit) {
                break;
            }

            strncpy_s(config->Target.m_OnlinePlayerNames[count], playerName.c_str(), _TRUNCATE);
            ++count;
        }

        config->Target.m_OnlinePlayersCount = count;
    }

    void SyncOnlinePlayersToConfigSafe(JNIEnv* env, World* world, jobject localPlayerObject, ModuleConfig* config) {
        if (!config) {
            return;
        }

        if (!env || !world) {
            ClearOnlinePlayersToConfig(config);
            return;
        }

        if (env->PushLocalFrame(256) != 0) {
            return;
        }

        SyncOnlinePlayersToConfig(env, world, localPlayerObject, config);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->PopLocalFrame(nullptr);
    }

    bool IsSameClanLikeHideClans(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard) {
        if (!env || !player || !localPlayer || !scoreboard) {
            return false;
        }

        const std::string localName = localPlayer->GetName(env, true);
        const std::string playerName = player->GetName(env, true);
        if (localName.empty() || playerName.empty()) {
            return false;
        }

        const std::string localFormatted = localPlayer->GetName(env, false);
        const std::string playerFormatted = player->GetName(env, false);
        const std::string localClanTag = NormalizeBrowseTag(localPlayer->GetClanTag(env, scoreboard));
        const std::string playerClanTag = NormalizeBrowseTag(player->GetClanTag(env, scoreboard));

        auto* localTeam = reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, localName));
        auto* playerTeam = reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, playerName));

        bool sameTeam = false;
        if (localTeam && playerTeam) {
            const std::string localRegisteredName = localTeam->GetRegisteredName(env);
            const std::string playerRegisteredName = playerTeam->GetRegisteredName(env);
            if (!localRegisteredName.empty() && localRegisteredName == playerRegisteredName) {
                sameTeam = true;
            } else {
                sameTeam = localTeam->IsSameTeam(env, playerTeam);
            }
        }

        if (!sameTeam && !localClanTag.empty() && !playerClanTag.empty()) {
            sameTeam = localClanTag == playerClanTag;
        }

        if (!sameTeam && IsAllyColorName(localFormatted) && IsAllyColorName(playerFormatted)) {
            sameTeam = true;
        }

        if (playerTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(playerTeam));
        }
        if (localTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(localTeam));
        }

        return sameTeam;
    }

    bool MatchesBrowseCache(JNIEnv* env, Player* player, Scoreboard* scoreboard, const BrowseCacheState& cache) {
        if (!env || !player || !scoreboard || !cache.active) {
            return false;
        }

        const std::string playerName = player->GetName(env, true);
        if (playerName.empty()) {
            return false;
        }

        bool matches = false;
        auto* playerTeam = reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, playerName));
        if (!cache.registeredTeamName.empty() && playerTeam) {
            const std::string registeredName = playerTeam->GetRegisteredName(env);
            matches = !registeredName.empty() && registeredName == cache.registeredTeamName;
        }

        if (!matches && !cache.normalizedClanTag.empty()) {
            const std::string normalizedTag = NormalizeBrowseTag(player->GetClanTag(env, scoreboard));
            matches = !normalizedTag.empty() && normalizedTag == cache.normalizedClanTag;
        }

        if (playerTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(playerTeam));
        }

        return matches;
    }

    bool IsRenderableBrowseCandidate(JNIEnv* env, Player* player) {
        if (!env || !player) {
            return false;
        }

        const float health = player->GetRealHealth(env);
        return health > 0.0f && !player->IsInvisible(env);
    }

    void RefreshBrowseMembers(JNIEnv* env, World* world, Scoreboard* scoreboard) {
        if (!env || !world || !scoreboard) {
            return;
        }

        BrowseCacheState cacheSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            cacheSnapshot = g_BrowseCache;
        }
        if (!cacheSnapshot.active) {
            return;
        }

        std::unordered_set<std::string> discoveredMembers;
        jobject localPlayerObject = Minecraft::GetThePlayer(env);
        const auto players = world->GetPlayerEntities(env);
        for (auto* player : players) {
            if (!player) {
                continue;
            }

            if (localPlayerObject && env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                continue;
            }

            if (MatchesBrowseCache(env, player, scoreboard, cacheSnapshot)) {
                const std::string playerName = player->GetName(env, true);
                if (!playerName.empty()) {
                    discoveredMembers.insert(playerName);
                }
            }

            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }

        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }

        CollectBrowseMembersFromNetHandler(env, cacheSnapshot, discoveredMembers);

        if (discoveredMembers.empty()) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            if (!g_BrowseCache.active ||
                g_BrowseCache.normalizedClanTag != cacheSnapshot.normalizedClanTag ||
                g_BrowseCache.registeredTeamName != cacheSnapshot.registeredTeamName) {
                return;
            }

            g_BrowseCache.members.insert(discoveredMembers.begin(), discoveredMembers.end());
            cacheSnapshot = g_BrowseCache;
        }

        SyncBrowseCacheToConfig(cacheSnapshot);
    }

}

void Target::OnEntityAttacked(JNIEnv* env, Player* attackedPlayer) {
    if (!env || !attackedPlayer) {
        return;
    }

    auto* config = Bridge::Get()->GetConfig();
    if (!config || !config->Target.m_BrowseAllPlayers) {
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    auto* scoreboard = reinterpret_cast<Scoreboard*>(world->GetScoreboard(env));
    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    auto* localPlayer = localPlayerObject ? reinterpret_cast<Player*>(localPlayerObject) : nullptr;
    if (localPlayer && IsSameClanLikeHideClans(env, attackedPlayer, localPlayer, scoreboard)) {
        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }
        if (scoreboard) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
        }
        env->DeleteLocalRef(worldObject);
        return;
    }

    const std::string attackedName = attackedPlayer->GetName(env, true);
    const std::string plainClanTag = attackedPlayer->GetClanTag(env, scoreboard);
    const std::string formattedClanTag = attackedPlayer->GetFormattedClanTag(env, scoreboard);
    const std::string normalizedClanTag = NormalizeBrowseTag(plainClanTag);

    auto* attackedTeam = scoreboard && !attackedName.empty()
        ? reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, attackedName))
        : nullptr;
    const std::string registeredTeamName = attackedTeam ? attackedTeam->GetRegisteredName(env) : "";

    if (normalizedClanTag.empty() && registeredTeamName.empty()) {
        if (attackedTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(attackedTeam));
        }
        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }
        if (scoreboard) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
        }
        env->DeleteLocalRef(worldObject);
        return;
    }

    std::unordered_set<std::string> matchedPlayers;
    const auto players = world->GetPlayerEntities(env);
    for (auto* player : players) {
        if (!player) {
            continue;
        }

        if (localPlayerObject && env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        if (playerName.empty()) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        bool matches = false;
        auto* playerTeam = scoreboard ? reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, playerName)) : nullptr;
        if (!registeredTeamName.empty() && playerTeam) {
            const std::string playerRegisteredName = playerTeam->GetRegisteredName(env);
            matches = !playerRegisteredName.empty() && playerRegisteredName == registeredTeamName;
            if (!matches && attackedTeam) {
                matches = attackedTeam->IsSameTeam(env, playerTeam);
            }
        }

        if (!matches && !normalizedClanTag.empty()) {
            const std::string normalizedPlayerTag = NormalizeBrowseTag(player->GetClanTag(env, scoreboard));
            matches = !normalizedPlayerTag.empty() && normalizedPlayerTag == normalizedClanTag;
        }

        if (playerTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(playerTeam));
        }

        if (matches) {
            matchedPlayers.insert(playerName);
        }

        env->DeleteLocalRef(reinterpret_cast<jobject>(player));
    }

    {
        std::lock_guard<std::mutex> lock(g_BrowseMutex);
        const bool sameGroup =
            g_BrowseCache.active &&
            g_BrowseCache.normalizedClanTag == normalizedClanTag &&
            g_BrowseCache.registeredTeamName == registeredTeamName;

        if (!sameGroup) {
            g_BrowseCache = {};
            g_BrowseCache.active = true;
            g_BrowseCache.clanTagDisplay = !formattedClanTag.empty()
                ? formattedClanTag
                : (!plainClanTag.empty() ? plainClanTag : registeredTeamName);
            g_BrowseCache.normalizedClanTag = normalizedClanTag;
            g_BrowseCache.registeredTeamName = registeredTeamName;
        }

        g_BrowseCache.members.insert(matchedPlayers.begin(), matchedPlayers.end());
        if (!attackedName.empty()) {
            g_BrowseCache.members.insert(attackedName);
            g_BrowseCache.processed.insert(attackedName);
        }
    }

    SyncBrowseCacheToConfig();

    if (attackedTeam) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(attackedTeam));
    }
    if (localPlayerObject) {
        env->DeleteLocalRef(localPlayerObject);
    }
    if (scoreboard) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
    }
    env->DeleteLocalRef(worldObject);
}

void Target::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    auto* config = Bridge::Get()->GetConfig();
    if (!config) {
        return;
    }

    if (!env) {
        ClearOnlinePlayersToConfig(config);
        ClearTargetHealthCache();
        return;
    }

    if (config->Target.m_BrowseClearCacheRequested) {
        ClearBrowseCache();
        config->Target.m_BrowseClearCacheRequested = false;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        ClearOnlinePlayersToConfig(config);
        ClearTargetHealthCache();
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    jobject localPlayerObject = Minecraft::GetThePlayer(env);

    if (!IsEnabled()) {
        g_TargetActiveManages.store(false);
        if (m_WasEnabled) {
            const auto players = world->GetPlayerEntities(env);
            for (auto* player : players) {
                if (player) {
                    player->Restore(env);
                    env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                }
            }
            ClearLockedTarget();
            m_WasEnabled = false;
        }
        m_PreviousSwingProgressInt = 0;
        m_PreviousPhysicalClick = false;
        ClearInUse();
        ClearTargetHealthCache();
        SyncOnlinePlayersToConfigSafe(env, world, localPlayerObject, config);
        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }
        env->DeleteLocalRef(worldObject);
        return;
    }

    g_TargetActiveManages.store(true);
    m_WasEnabled = true;

    if (!localPlayerObject) {
        env->DeleteLocalRef(worldObject);
        return;
    }

    auto* localPlayer = reinterpret_cast<Player*>(localPlayerObject);
    SyncTargetHealthCache(env, world, localPlayerObject);
    const bool isClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const int swingProgress = localPlayer->GetSwingProgressInt(env);
    const bool newObservedHit = (swingProgress == 1 && m_PreviousSwingProgressInt != 1) || (isClicking && !m_PreviousPhysicalClick);

    struct ObservedHitInfo {
        bool browseActivated = false;
        std::string playerName;
    };

    auto observeHit = [&]() -> ObservedHitInfo {
        ObservedHitInfo info;
        if (!newObservedHit) {
            return info;
        }

        jobject mouseOverObject = Minecraft::GetObjectMouseOver(env);
        if (!mouseOverObject) {
            return info;
        }

        auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject);
        if (mouseOver->IsAimingEntity(env)) {
            jobject attackedObject = mouseOver->GetEntity(env);
            if (attackedObject && !env->IsSameObject(attackedObject, localPlayerObject)) {
                info.playerName = reinterpret_cast<Player*>(attackedObject)->GetName(env, true);
                OnEntityAttacked(env, reinterpret_cast<Player*>(attackedObject));
                info.browseActivated = GetBrowseDisplayInfo().active;
            }
            if (attackedObject) {
                env->DeleteLocalRef(attackedObject);
            }
        }

        env->DeleteLocalRef(mouseOverObject);
        return info;
    };

    const bool automaticEnabled = config->Target.m_AutoTarget || config->Target.m_TargetSwitch;
    if (automaticEnabled) {
        auto selectBestAutomaticTarget = [&]() {
            AutoSelectTarget(env);
        };

        auto selectBestBrowseTarget = [&]() -> bool {
            auto* scoreboard = reinterpret_cast<Scoreboard*>(world->GetScoreboard(env));
            const std::string previousTarget = GetLockedTarget();
            std::string nextTarget;
            const bool selected = TrySelectBrowseTarget(env, localPlayer, world, scoreboard, previousTarget, nextTarget);
            if (scoreboard) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
            }

            if (!selected || nextTarget.empty()) {
                return false;
            }

            SetLockedTarget(nextTarget);
            m_BrowseHitCount = 0;
            m_LastBrowseSwitchTime = std::chrono::steady_clock::now();
            return true;
        };

        auto isLockedBrowseTargetRendered = [&]() -> bool {
            const std::string lockedTarget = GetLockedTarget();
            if (lockedTarget.empty()) {
                return false;
            }

            BrowseCacheState cacheSnapshot;
            {
                std::lock_guard<std::mutex> lock(g_BrowseMutex);
                cacheSnapshot = g_BrowseCache;
            }
            if (!cacheSnapshot.active) {
                return false;
            }

            auto* scoreboard = reinterpret_cast<Scoreboard*>(world->GetScoreboard(env));
            const auto players = world->GetPlayerEntities(env);
            bool found = false;
            for (auto* player : players) {
                if (!player) {
                    continue;
                }

                if (env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
                    env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                    continue;
                }

                const std::string playerName = player->GetName(env, true);
                if (TargetNamesMatch(playerName, lockedTarget) &&
                    MatchesBrowseCache(env, player, scoreboard, cacheSnapshot) &&
                    IsRenderableBrowseCandidate(env, player)) {
                    found = true;
                    env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                    break;
                }

                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            }

            if (scoreboard) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
            }
            return found;
        };

        bool browseActive = false;
        if (config->Target.m_BrowseAllPlayers) {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            browseActive = g_BrowseCache.active;
        }

        if (config->Target.m_BrowseAllPlayers && !browseActive && HasLockedTarget()) {
            ClearLockedTarget();
        }

        ObservedHitInfo observedHit;
        if (config->Target.m_BrowseAllPlayers && newObservedHit) {
            observedHit = observeHit();
            if (observedHit.browseActivated) {
                browseActive = true;
            }
            if (!observedHit.playerName.empty() && TargetNamesMatch(observedHit.playerName, GetLockedTarget())) {
                ++m_BrowseHitCount;
            }
        }

        if (config->Target.m_BrowseAllPlayers) {
            if (browseActive) {
                const bool currentRendered = isLockedBrowseTargetRendered();
                bool shouldSwitch = !currentRendered;
                if (!shouldSwitch) {
                    if (config->Target.m_SwitchMode == 0) {
                        shouldSwitch = m_BrowseHitCount >= config->Target.m_SwitchHits;
                    } else {
                        if (m_LastBrowseSwitchTime.time_since_epoch().count() == 0) {
                            m_LastBrowseSwitchTime = std::chrono::steady_clock::now();
                        }
                        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - m_LastBrowseSwitchTime).count();
                        shouldSwitch = elapsedMs >= config->Target.m_SwitchTimeMs;
                    }
                }

                if (shouldSwitch) {
                    if (currentRendered) {
                        MarkBrowsePlayerProcessed(GetLockedTarget());
                    }

                    const bool selected = selectBestBrowseTarget();
                    if (!selected) {
                        ClearLockedTarget();
                        m_BrowseHitCount = 0;
                    }
                }
            } else {
                if (HasLockedTarget()) {
                    ClearLockedTarget();
                }
                m_BrowseHitCount = 0;
            }
        } else {
            selectBestAutomaticTarget();
            m_BrowseHitCount = 0;
        }

        ManageHitboxes(env, localPlayer, world);
    } else {
        const std::string targetName = config->Target.m_PlayerName;
        SetLockedTarget(targetName);
        const auto players = world->GetPlayerEntities(env);
        bool affectedPlayers = false;
        for (auto* player : players) {
            if (!player) {
                continue;
            }

            if (env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                continue;
            }

            const std::string playerName = player->GetName(env, true);
            if (!targetName.empty() && TargetNamesMatch(playerName, targetName)) {
                try { player->Restore(env); } catch (...) {}
            } else {
                try { player->Zero(env); } catch (...) {}
                affectedPlayers = true;
            }

            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }

        if (affectedPlayers) {
            MarkInUse(200);
        } else {
            ClearInUse();
        }
    }

    if (automaticEnabled) {
        bool browseActiveNow = false;
        {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            browseActiveNow = g_BrowseCache.active;
        }
        if (HasLockedTarget() || browseActiveNow) {
            MarkInUse(200);
        } else {
            ClearInUse();
        }
    }

    SyncOnlinePlayersToConfigSafe(env, world, localPlayerObject, config);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    m_PreviousPhysicalClick = isClicking;
    m_PreviousSwingProgressInt = swingProgress;
    env->DeleteLocalRef(localPlayerObject);
    env->DeleteLocalRef(worldObject);
}

void Target::RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
    (void)screenW;
    (void)screenH;

    if (!IsEnabled() || !drawList || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    auto* config = Bridge::Get()->GetConfig();
    if (!config) {
        return;
    }

    JNIEnv* env = g_Game->GetCurrentEnv();
    if (!env) {
        return;
    }

    jobject currentScreenObject = Minecraft::GetCurrentScreen(env);
    if (currentScreenObject) {
        env->DeleteLocalRef(currentScreenObject);
        return;
    }

    const bool automaticEnabled = config->Target.m_AutoTarget || config->Target.m_TargetSwitch;
    std::string targetName;
    if (automaticEnabled) {
        targetName = GetLockedTarget();
    } else {
        targetName = config->Target.m_PlayerName;
    }

    if (ShouldSkipTargetEspForBadlionThirdPerson(env)) {
        return;
    }

    jobject timerObject = Minecraft::GetTimer(env);
    jobject worldObject = Minecraft::GetTheWorld(env);
    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    if (!timerObject || !worldObject || !localPlayerObject) {
        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }
        if (worldObject) {
            env->DeleteLocalRef(worldObject);
        }
        if (timerObject) {
            env->DeleteLocalRef(timerObject);
        }
        return;
    }

    auto* timer = reinterpret_cast<Timer*>(timerObject);
    auto* world = reinterpret_cast<World*>(worldObject);
    auto* localPlayer = reinterpret_cast<Player*>(localPlayerObject);
    const float partialTicks = timer->GetRenderPartialTicks(env);
    env->DeleteLocalRef(timerObject);

    const Vec3D localPos = localPlayer->GetPos(env);
    const Vec3D localLastPos = localPlayer->GetLastTickPos(env);
    const double localViewX = localLastPos.x + (localPos.x - localLastPos.x) * partialTicks;
    const double localViewY = localLastPos.y + (localPos.y - localLastPos.y) * partialTicks;
    const double localViewZ = localLastPos.z + (localPos.z - localLastPos.z) * partialTicks;

    const RenderMatrixSnapshot renderSnapshot = CaptureRenderMatrixSnapshot();
    if (!renderSnapshot.IsValid()) {
        env->DeleteLocalRef(localPlayerObject);
        env->DeleteLocalRef(worldObject);
        return;
    }

    const int thirdPersonView = Minecraft::GetThirdPersonView(env);
    const auto gameVersion = g_Game->GetGameVersion();
    const bool allowLocalPlayerTargetEsp = thirdPersonView != 0;
    const bool useRelativeThirdPersonProjection =
        thirdPersonView != 0 &&
        gameVersion == GameVersions::LUNAR;
    Vec3D thirdPersonCameraPos{};
    bool hasThirdPersonCameraPos = false;
    jobject renderManagerObject = nullptr;
    if (useRelativeThirdPersonProjection) {
        renderManagerObject = Minecraft::GetRenderManager(env);
        if (renderManagerObject) {
            hasThirdPersonCameraPos = TryComputeThirdPersonCameraPosition(
                env,
                localPlayer,
                reinterpret_cast<RenderManager*>(renderManagerObject),
                partialTicks,
                thirdPersonView,
                gameVersion,
                thirdPersonCameraPos);
        }
    }

    if (targetName.empty()) {
        if (renderManagerObject) {
            env->DeleteLocalRef(renderManagerObject);
        }
        env->DeleteLocalRef(localPlayerObject);
        env->DeleteLocalRef(worldObject);
        return;
    }

    ImDrawList* backgroundDrawList = ImGui::GetBackgroundDrawList();
    if (!backgroundDrawList) {
        backgroundDrawList = drawList;
    }

    bool drewAnyEsp = false;
    const auto players = world->GetPlayerEntities(env);
    for (auto* player : players) {
        if (!player) {
            continue;
        }

        const bool isLocalPlayer = env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject);
        if (isLocalPlayer && !allowLocalPlayerTargetEsp) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        if (!TargetNamesMatch(playerName, targetName)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        if (player->GetHealth(env) <= 0.0f) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        float realHealth = player->GetRealHealth(env);
        if (realHealth < 0.0f) {
            realHealth = GetCachedTargetRealHealth(playerName);
        }
        if (realHealth < 0.0f) {
            realHealth = player->GetHealth(env);
        } else {
            UpdateTargetRealHealth(playerName, realHealth);
        }

        if (player->IsInvisible(env)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const Vec3D playerPos = player->GetPos(env);
        const Vec3D playerLastPos = player->GetLastTickPos(env);
        const double playerViewX = playerLastPos.x + (playerPos.x - playerLastPos.x) * partialTicks;
        const double playerViewY = playerLastPos.y + (playerPos.y - playerLastPos.y) * partialTicks;
        const double playerViewZ = playerLastPos.z + (playerPos.z - playerLastPos.z) * partialTicks;

        const double dx = playerViewX - localViewX;
        const double dy = playerViewY - localViewY;
        const double dz = playerViewZ - localViewZ;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > 255.0) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        float width = player->GetWidth(env);
        float height = player->GetHeight(env);
        if (width <= 0.05f) {
            width = 0.6f;
        }
        if (height <= 0.05f) {
            height = 1.8f;
        }

        ProjectedTargetBox projectedBox;
        bool hasProjectedBox = false;
        if (hasThirdPersonCameraPos) {
            hasProjectedBox = TryBuildProjectedThirdPersonTargetBox(
                thirdPersonCameraPos,
                playerPos,
                playerLastPos,
                width,
                height,
                partialTicks,
                renderSnapshot,
                projectedBox);
        }

        if (!hasProjectedBox && !useRelativeThirdPersonProjection) {
            hasProjectedBox = TryBuildProjectedTargetBox(
                playerViewX,
                playerViewY,
                playerViewZ,
                width,
                height,
                renderSnapshot,
                projectedBox);
        }

        if (!hasProjectedBox) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        float maxHealth = player->GetMaxHealth(env);
        const int hurtTime = player->GetHurtTime(env);
        const ImU32 color = BuildTargetEspColor(realHealth, maxHealth, hurtTime);
        DrawProjectedTargetBox(backgroundDrawList, projectedBox, color);
        DrawTargetHealthBar(backgroundDrawList, projectedBox, realHealth, maxHealth, color);

        drewAnyEsp = true;
        env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        break;
    }

    if (drewAnyEsp) {
        MarkInUse(120);
    }

    if (renderManagerObject) {
        env->DeleteLocalRef(renderManagerObject);
    }
    env->DeleteLocalRef(localPlayerObject);
    env->DeleteLocalRef(worldObject);
}

std::string Target::GetLockedTarget() const {
    std::lock_guard<std::mutex> lock(m_TargetMutex);
    return m_LockedTargetName;
}

void Target::SetLockedTarget(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(m_TargetMutex);
        m_LockedTargetName = name;
    }

    std::lock_guard<std::mutex> lock(g_CurrentTargetMutex);
    g_CurrentTargetName = name;
}

void Target::ClearLockedTarget() {
    {
        std::lock_guard<std::mutex> lock(m_TargetMutex);
        m_LockedTargetName.clear();
    }

    std::lock_guard<std::mutex> lock(g_CurrentTargetMutex);
    g_CurrentTargetName.clear();
}

bool Target::HasLockedTarget() const {
    std::lock_guard<std::mutex> lock(m_TargetMutex);
    return !m_LockedTargetName.empty();
}

std::string Target::GetCurrentTargetName() {
    std::lock_guard<std::mutex> lock(g_CurrentTargetMutex);
    return g_CurrentTargetName;
}

Target::BrowseDisplayInfo Target::GetBrowseDisplayInfo() {
    std::lock_guard<std::mutex> lock(g_BrowseMutex);
    BrowseDisplayInfo info;
    info.active = g_BrowseCache.active;
    info.clanTag = g_BrowseCache.clanTagDisplay;
    info.currentPlayer = GetCurrentTargetName();
    info.remainingPlayers = (std::max)(0, static_cast<int>(g_BrowseCache.members.size()) - static_cast<int>(g_BrowseCache.processed.size()));
    return info;
}

void Target::ClearBrowseCache() {
    {
        std::lock_guard<std::mutex> lock(g_BrowseMutex);
        g_BrowseCache = {};
    }

    SyncBrowseCacheToConfig();
}

bool Target::IsTargetActivelyManaging() {
    return g_TargetActiveManages.load();
}

void Target::ManageHitboxes(JNIEnv* env, Player* localPlayer, World* world) {
    auto* config = Bridge::Get()->GetConfig();
    const std::string lockedTarget = GetLockedTarget();
    const bool automaticEnabled = config && (config->Target.m_AutoTarget || config->Target.m_TargetSwitch);
    const bool browseWaitingMode = automaticEnabled && config->Target.m_BrowseAllPlayers;

    bool browseActive = false;
    if (browseWaitingMode) {
        std::lock_guard<std::mutex> lock(g_BrowseMutex);
        browseActive = g_BrowseCache.active;
    }

    const auto players = world->GetPlayerEntities(env);
    for (auto* player : players) {
        if (!player) {
            continue;
        }

        if (env->IsSameObject(reinterpret_cast<jobject>(player), reinterpret_cast<jobject>(localPlayer))) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        if (lockedTarget.empty()) {
            if (browseWaitingMode && browseActive) {
                try { player->Zero(env); } catch (...) {}
            } else {
                try { player->Restore(env); } catch (...) {}
            }
        } else {
            const std::string playerName = player->GetName(env, true);
            if (TargetNamesMatch(playerName, lockedTarget)) {
                try { player->Restore(env); } catch (...) {}
            } else {
                try { player->Zero(env); } catch (...) {}
            }
        }

        env->DeleteLocalRef(reinterpret_cast<jobject>(player));
    }
}

void Target::AutoSelectTarget(JNIEnv* env) {
    jobject worldObject = Minecraft::GetTheWorld(env);
    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    if (!worldObject || !localPlayerObject) {
        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }
        if (worldObject) {
            env->DeleteLocalRef(worldObject);
        }
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    auto* localPlayer = reinterpret_cast<Player*>(localPlayerObject);
    auto* scoreboard = reinterpret_cast<Scoreboard*>(world->GetScoreboard(env));
    const auto players = world->GetPlayerEntities(env);
    const std::string currentTarget = GetLockedTarget();

    Player* bestTarget = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    int processed = 0;

    for (auto* player : players) {
        if (!player || processed++ >= kMaxPlayersToProcess) {
            if (player) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            }
            continue;
        }

        if (env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        if (!IsValidCombatTarget(env, player, localPlayer) || IsSameClan(env, player, localPlayer, scoreboard)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const float score = CalculatePriorityScore(env, player, localPlayer, scoreboard);
        const std::string playerName = player->GetName(env, true);
        const bool keepCurrentTie = score == bestScore && !currentTarget.empty() && TargetNamesMatch(playerName, currentTarget);
        if (score < bestScore || keepCurrentTie) {
            bestScore = score;
            if (bestTarget) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
            }
            bestTarget = player;
        } else {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }
    }

    if (bestTarget) {
        SetLockedTarget(bestTarget->GetName(env, true));
        env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
    } else {
        ClearLockedTarget();
    }

    if (scoreboard) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
    }
    env->DeleteLocalRef(localPlayerObject);
    env->DeleteLocalRef(worldObject);
}

bool Target::TrySelectBrowseTarget(JNIEnv* env, Player* localPlayer, World* world, Scoreboard* scoreboard, const std::string& previousTarget, std::string& nextTarget) {
    auto* config = Bridge::Get()->GetConfig();
    if (!config || !config->Target.m_BrowseAllPlayers) {
        return false;
    }

    RefreshBrowseMembers(env, world, scoreboard);

    {
        bool resetCycle = false;
        std::lock_guard<std::mutex> lock(g_BrowseMutex);
        if (g_BrowseCache.active &&
            !g_BrowseCache.members.empty() &&
            g_BrowseCache.processed.size() >= g_BrowseCache.members.size()) {
            g_BrowseCache.processed.clear();
            resetCycle = true;
        }

        if (resetCycle) {
            SyncBrowseCacheToConfig(g_BrowseCache);
        }
    }

    BrowseCacheState cacheSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_BrowseMutex);
        cacheSnapshot = g_BrowseCache;
    }
    if (!cacheSnapshot.active || cacheSnapshot.members.empty()) {
        return false;
    }

    Player* bestTarget = nullptr;
    Player* processedFallbackTarget = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    float processedFallbackScore = std::numeric_limits<float>::max();
    const auto players = world->GetPlayerEntities(env);

    for (auto* player : players) {
        if (!player) {
            continue;
        }

        if (env->IsSameObject(reinterpret_cast<jobject>(player), reinterpret_cast<jobject>(localPlayer))) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        if (!MatchesBrowseCache(env, player, scoreboard, cacheSnapshot) || !IsRenderableBrowseCandidate(env, player)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        if (playerName.empty()) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const float score = localPlayer->GetDistanceToEntity(reinterpret_cast<jobject>(player), env);
        if (cacheSnapshot.processed.count(playerName) > 0) {
            if (score < processedFallbackScore ||
                (score == processedFallbackScore && !previousTarget.empty() && TargetNamesMatch(playerName, previousTarget))) {
                if (processedFallbackTarget) {
                    env->DeleteLocalRef(reinterpret_cast<jobject>(processedFallbackTarget));
                }
                processedFallbackTarget = player;
                processedFallbackScore = score;
            } else {
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            }
            continue;
        }

        if (score < bestScore ||
            (score == bestScore && !previousTarget.empty() && TargetNamesMatch(playerName, previousTarget))) {
            if (bestTarget) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
            }
            bestTarget = player;
            bestScore = score;
        } else {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }
    }

    if (!bestTarget && processedFallbackTarget) {
        BrowseCacheState snapshot;
        {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            if (g_BrowseCache.active &&
                g_BrowseCache.normalizedClanTag == cacheSnapshot.normalizedClanTag &&
                g_BrowseCache.registeredTeamName == cacheSnapshot.registeredTeamName) {
                g_BrowseCache.processed.clear();
                snapshot = g_BrowseCache;
            }
        }

        if (snapshot.active) {
            SyncBrowseCacheToConfig(snapshot);
        }

        bestTarget = processedFallbackTarget;
        processedFallbackTarget = nullptr;
    }

    if (processedFallbackTarget) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(processedFallbackTarget));
    }

    if (!bestTarget) {
        return false;
    }

    nextTarget = bestTarget->GetName(env, true);
    env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
    return !nextTarget.empty();
}

bool Target::IsValidCombatTarget(JNIEnv* env, Player* player, Player* localPlayer) {
    if (!player || !localPlayer) {
        return false;
    }

    const float health = player->GetRealHealth(env);
    if (health <= 0.0f || player->IsInvisible(env)) {
        return false;
    }

    return localPlayer->GetDistanceToEntity(reinterpret_cast<jobject>(player), env) <= static_cast<float>(kMaxTargetDistance);
}

float Target::CalculatePriorityScore(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard) {
    (void)scoreboard;

    auto* config = Bridge::Get()->GetConfig();
    if (!config) {
        return std::numeric_limits<float>::max();
    }

    const float health = player->GetRealHealth(env);
    const float breakArmorVulnerability = GetBreakArmorVulnerability(env, player);
    const float lowArmorScore = GetLowArmorScore(env, player);
    const float distance = localPlayer->GetDistanceToEntity(reinterpret_cast<jobject>(player), env);

    switch (config->Target.m_PriorityMode) {
    case kModeBreakArmor:
        return -breakArmorVulnerability + (distance * 0.001f);
    case kModeHealth:
        return health + (distance * 0.001f);
    case kModeBoth:
        return (health * config->Target.m_BothHealthWeight) +
            (distance * 0.001f) -
            (breakArmorVulnerability * config->Target.m_BothArmorWeight);
    case kModeBrowseAllPlayers:
        return distance;
    case kModeLowArmor:
    default:
        return lowArmorScore + (distance * 0.001f);
    }
}

float Target::GetBreakArmorVulnerability(JNIEnv* env, Player* player) {
    auto* config = Bridge::Get()->GetConfig();
    if (!config) {
        return 0.0f;
    }

    float vulnerability = 0.0f;
    for (int slot = 0; slot < 4; ++slot) {
        jobject armorObject = player->GetCurrentArmor(slot, env);
        if (armorObject) {
            auto* armorStack = reinterpret_cast<ItemStack*>(armorObject);
            jobject itemObject = armorStack->GetItem(env);
            int armorValue = itemObject ? ItemArmor::GetDamageReduceAmount(itemObject, env) : 0;
            if (itemObject) {
                env->DeleteLocalRef(itemObject);
            }

            if (armorValue <= 0) {
                armorValue = 1;
            }

            float pieceVulnerability = static_cast<float>(armorValue);
            if (config->Target.m_ConsiderDurability) {
                const int maxDamage = armorStack->GetMaxDamage(env);
                const int itemDamage = armorStack->GetItemDamage(env);
                if (maxDamage > 0) {
                    const float rawDamageRatio = static_cast<float>(itemDamage) / static_cast<float>(maxDamage);
                    const float damageRatio = (std::max)(0.0f, (std::min)(1.0f, rawDamageRatio));
                    const float durabilityWeight = 1.0f + (config->Target.m_BrokenArmorPriority * 0.25f);
                    pieceVulnerability = (static_cast<float>(armorValue) * 0.35f) +
                        (static_cast<float>(armorValue) * damageRatio * durabilityWeight);

                    if (damageRatio >= 0.85f) {
                        pieceVulnerability += config->Target.m_BrokenArmorPriority;
                    }
                }
            }

            vulnerability += pieceVulnerability;
            env->DeleteLocalRef(armorObject);
        }

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    return vulnerability;
}

float Target::GetLowArmorScore(JNIEnv* env, Player* player) {
    float remainingDurability = 0.0f;
    for (int slot = 0; slot < 4; ++slot) {
        jobject armorObject = player->GetCurrentArmor(slot, env);
        if (!armorObject) {
            continue;
        }

        auto* armorStack = reinterpret_cast<ItemStack*>(armorObject);
        const int maxDamage = armorStack->GetMaxDamage(env);
        const int itemDamage = armorStack->GetItemDamage(env);
        if (maxDamage > 0) {
            const int safeDamage = (std::max)(0, (std::min)(maxDamage, itemDamage));
            remainingDurability += static_cast<float>(maxDamage - safeDamage);
        }

        env->DeleteLocalRef(armorObject);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    return remainingDurability;
}

bool Target::IsSameClan(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard) {
    return IsSameClanLikeHideClans(env, player, localPlayer, scoreboard);
}
