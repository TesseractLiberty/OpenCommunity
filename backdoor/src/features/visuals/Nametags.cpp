#include "pch.h"
#include "Nametags.h"

#include "../../core/RenderHook.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/Scoreboard.h"
#include "../../game/classes/Team.h"
#include "../../game/classes/Timer.h"
#include "../../game/classes/World.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/mapping/Mapper.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
    constexpr float kNametagVerticalOffset = 0.0f;
    constexpr float kNametagHorizontalPadding = 9.0f;
    constexpr float kNametagVerticalPadding = 6.0f;
    constexpr float kNametagCornerRounding = 7.0f;
    constexpr float kNametagBaseFontSize = 15.5f;
    constexpr float kNametagSegmentSpacing = 5.0f;
    constexpr float kNametagShadowOffset = 1.0f;
    constexpr float kHeartVerticalOffset = 3.25f;
    constexpr float kMinimumNametagScale = 0.72f;
    constexpr float kMaximumNametagScale = 1.08f;
    constexpr float kNametagTopOffset = 4.0f;
    constexpr float kMaximumRenderableDistance = 255.0f;
    constexpr const char* kHiddenNametagTeamName = "__oc_nametags_hidden";
    constexpr float kScaleSmoothingStrength = 14.0f;
    constexpr auto kSmoothingResetDelay = std::chrono::milliseconds(450);
    constexpr auto kHealthCacheRefreshInterval = std::chrono::milliseconds(90);
    constexpr auto kVanillaHideRefreshInterval = std::chrono::milliseconds(275);

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

    struct ProjectedNametagBox {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
    };

    struct NametagRenderEntry {
        std::string name;
        float realHealth = 0.0f;
        float maxHealth = 20.0f;
        float distance = 0.0f;
        ScreenPoint anchor;
        float scale = 1.0f;
    };

    struct SmoothedNametagState {
        ScreenPoint anchor;
        float scale = 1.0f;
        std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();
    };

    struct CachedNametagStats {
        float realHealth = 0.0f;
        float maxHealth = 20.0f;
        std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();
    };

    std::mutex g_NametagStateMutex;
    std::unordered_map<std::string, jobject> g_OriginalTeamVisibilityRefs;
    std::unordered_set<std::string> g_InjectedPlayers;
    std::unordered_map<std::string, SmoothedNametagState> g_SmoothedNametags;
    std::unordered_map<std::string, CachedNametagStats> g_CachedNametagStats;
    std::chrono::steady_clock::time_point g_LastHealthCacheRefresh = std::chrono::steady_clock::time_point::min();
    std::chrono::steady_clock::time_point g_LastVanillaHideRefresh = std::chrono::steady_clock::time_point::min();

    float Clamp01(float value) {
        return (std::max)(0.0f, (std::min)(1.0f, value));
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

        if (ndcZ < -1.0f || ndcZ > 1.0f) {
            return false;
        }

        screenPoint.x = snapshot.viewportWidth * ((ndcX + 1.0f) * 0.5f);
        screenPoint.y = snapshot.viewportHeight * ((1.0f - ndcY) * 0.5f);
        return std::isfinite(screenPoint.x) && std::isfinite(screenPoint.y);
    }

    bool TryBuildProjectedNametagBox(
        double entityX,
        double entityY,
        double entityZ,
        float entityWidth,
        float entityHeight,
        const RenderMatrixSnapshot& snapshot,
        ProjectedNametagBox& projectedBox) {
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

    float ComputeHealthRatio(float realHealth, float maxHealth) {
        float safeMaxHealth = maxHealth;
        if (safeMaxHealth <= 0.0f) {
            safeMaxHealth = (std::max)(20.0f, realHealth);
        }
        if (realHealth > safeMaxHealth) {
            safeMaxHealth = realHealth;
        }

        return Clamp01(realHealth / safeMaxHealth);
    }

    ImU32 BuildHealthColor(float realHealth, float maxHealth) {
        const float ratio = ComputeHealthRatio(realHealth, maxHealth);
        if (ratio >= 0.70f) {
            return IM_COL32(101, 214, 114, 255);
        }
        if (ratio >= 0.45f) {
            return IM_COL32(255, 213, 92, 255);
        }
        if (ratio >= 0.20f) {
            return IM_COL32(255, 160, 80, 255);
        }
        return IM_COL32(255, 96, 96, 255);
    }

    float ComputeNametagScale(float distance) {
        const float scaled = 1.05f - (distance * 0.0125f);
        return (std::max)(kMinimumNametagScale, (std::min)(kMaximumNametagScale, scaled));
    }

    jobject GetNeverVisibilityEnum(JNIEnv* env) {
        if (!env || !g_Game || !g_Game->IsInitialized()) {
            return nullptr;
        }

        const std::string enumClassName = Mapper::Get("net/minecraft/scoreboard/Team$EnumVisible");
        const std::string methodName = Mapper::Get("getEnumVisibleByName");
        const std::string visibilitySignature = Mapper::Get("net/minecraft/scoreboard/Team$EnumVisible", 2);
        if (enumClassName.empty() || methodName.empty() || visibilitySignature.empty()) {
            return nullptr;
        }

        jclass enumClass = reinterpret_cast<jclass>(g_Game->FindClass(enumClassName));
        if (!enumClass) {
            return nullptr;
        }

        const std::string signature = "(Ljava/lang/String;)" + visibilitySignature;
        jmethodID byNameMethod = env->GetStaticMethodID(enumClass, methodName.c_str(), signature.c_str());
        if (!byNameMethod) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return nullptr;
        }

        jstring neverString = env->NewStringUTF("never");
        jobject visibility = neverString ? env->CallStaticObjectMethod(enumClass, byNameMethod, neverString) : nullptr;
        if (neverString) {
            env->DeleteLocalRef(neverString);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }

        return visibility;
    }

    void DeleteStoredVisibilityRefs(JNIEnv* env) {
        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        for (auto& [teamName, visibilityRef] : g_OriginalTeamVisibilityRefs) {
            if (visibilityRef) {
                env->DeleteGlobalRef(visibilityRef);
            }
        }
        g_OriginalTeamVisibilityRefs.clear();
    }

    void ResetNametagSmoothing() {
        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        g_SmoothedNametags.clear();
    }

    void RestoreVanillaNameTags(JNIEnv* env, Scoreboard* scoreboard) {
        if (!env) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_NametagStateMutex);
            if (scoreboard) {
                for (const auto& [teamName, visibilityRef] : g_OriginalTeamVisibilityRefs) {
                    if (teamName.empty() || !visibilityRef) {
                        continue;
                    }

                    jobject teamObject = scoreboard->GetTeam(env, teamName);
                    if (!teamObject) {
                        continue;
                    }

                    reinterpret_cast<Team*>(teamObject)->SetNameTagVisibility(env, visibilityRef);
                    env->DeleteLocalRef(teamObject);
                }

                for (const auto& playerName : g_InjectedPlayers) {
                    if (!playerName.empty()) {
                        scoreboard->RemovePlayerFromTeams(env, playerName);
                    }
                }

                jobject hiddenTeamObject = scoreboard->GetTeam(env, kHiddenNametagTeamName);
                if (hiddenTeamObject) {
                    scoreboard->RemoveTeam(env, hiddenTeamObject);
                    env->DeleteLocalRef(hiddenTeamObject);
                }
            }

            for (auto& [teamName, visibilityRef] : g_OriginalTeamVisibilityRefs) {
                if (visibilityRef) {
                    env->DeleteGlobalRef(visibilityRef);
                }
            }
            g_OriginalTeamVisibilityRefs.clear();
            g_InjectedPlayers.clear();
            g_SmoothedNametags.clear();
            g_CachedNametagStats.clear();
            g_LastHealthCacheRefresh = std::chrono::steady_clock::time_point::min();
            g_LastVanillaHideRefresh = std::chrono::steady_clock::time_point::min();
        }
    }

    void EnsureTeamNameHidden(JNIEnv* env, Scoreboard* scoreboard, const std::string& teamName, Team* team, jobject neverVisibility, std::set<std::string>& activeTeamNames) {
        if (!env || !scoreboard || !team || !neverVisibility || teamName.empty()) {
            return;
        }

        activeTeamNames.insert(teamName);

        {
            std::lock_guard<std::mutex> lock(g_NametagStateMutex);
            if (g_OriginalTeamVisibilityRefs.find(teamName) == g_OriginalTeamVisibilityRefs.end()) {
                jobject originalVisibility = team->GetNameTagVisibility(env);
                if (originalVisibility) {
                    g_OriginalTeamVisibilityRefs.emplace(teamName, env->NewGlobalRef(originalVisibility));
                    env->DeleteLocalRef(originalVisibility);
                }
            }
        }

        team->SetNameTagVisibility(env, neverVisibility);
    }

    void CleanupInactiveTeamOverrides(JNIEnv* env, Scoreboard* scoreboard, const std::set<std::string>& activeTeamNames) {
        if (!env || !scoreboard) {
            return;
        }

        std::vector<std::pair<std::string, jobject>> teamsToRestore;
        {
            std::lock_guard<std::mutex> lock(g_NametagStateMutex);
            for (auto it = g_OriginalTeamVisibilityRefs.begin(); it != g_OriginalTeamVisibilityRefs.end();) {
                if (activeTeamNames.count(it->first) > 0) {
                    ++it;
                    continue;
                }

                teamsToRestore.emplace_back(it->first, it->second);
                it = g_OriginalTeamVisibilityRefs.erase(it);
            }
        }

        for (auto& [teamName, visibilityRef] : teamsToRestore) {
            if (!visibilityRef || teamName.empty()) {
                continue;
            }

            jobject teamObject = scoreboard->GetTeam(env, teamName);
            if (teamObject) {
                reinterpret_cast<Team*>(teamObject)->SetNameTagVisibility(env, visibilityRef);
                env->DeleteLocalRef(teamObject);
            }

            env->DeleteGlobalRef(visibilityRef);
        }
    }

    std::pair<ScreenPoint, float> SmoothNametagState(const std::string& name, const ScreenPoint& targetAnchor, float targetScale) {
        const auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        auto& state = g_SmoothedNametags[name];
        const auto elapsed = now - state.lastUpdate;
        if (elapsed > kSmoothingResetDelay) {
            state.anchor = targetAnchor;
            state.scale = targetScale;
            state.lastUpdate = now;
            return { state.anchor, state.scale };
        }

        const float deltaSeconds = (std::max)(0.0f, std::chrono::duration<float>(elapsed).count());
        const float scaleBlend = 1.0f - std::exp(-kScaleSmoothingStrength * deltaSeconds);
        state.anchor = targetAnchor;
        state.scale += (targetScale - state.scale) * scaleBlend;
        state.lastUpdate = now;
        return { state.anchor, state.scale };
    }

    void CleanupSmoothingCache(const std::unordered_set<std::string>& activeNames) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        for (auto it = g_SmoothedNametags.begin(); it != g_SmoothedNametags.end();) {
            if (activeNames.count(it->first) > 0 || (now - it->second.lastUpdate) <= kSmoothingResetDelay) {
                ++it;
                continue;
            }
            it = g_SmoothedNametags.erase(it);
        }
    }

    void UpdateCachedNametagStats(const std::string& playerName, float realHealth, float maxHealth) {
        if (playerName.empty()) {
            return;
        }

        CachedNametagStats stats;
        stats.realHealth = realHealth;
        stats.maxHealth = maxHealth;
        stats.lastUpdate = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        g_CachedNametagStats[playerName] = stats;
    }

    bool GetCachedNametagStats(const std::string& playerName, float& realHealth, float& maxHealth) {
        if (playerName.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        const auto it = g_CachedNametagStats.find(playerName);
        if (it == g_CachedNametagStats.end()) {
            return false;
        }

        realHealth = it->second.realHealth;
        maxHealth = it->second.maxHealth;
        return true;
    }

    void CleanupNametagStatsCache(const std::unordered_set<std::string>& activeNames) {
        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        for (auto it = g_CachedNametagStats.begin(); it != g_CachedNametagStats.end();) {
            if (activeNames.count(it->first) > 0) {
                ++it;
                continue;
            }

            it = g_CachedNametagStats.erase(it);
        }
    }

    std::string FormatHealthText(float realHealth) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f", realHealth);
        return buffer;
    }

    std::string FormatDistanceText(float distance) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%dm", static_cast<int>(std::roundf(distance)));
        return buffer;
    }

    ImVec2 CalcTextSize(ImFont* font, float fontSize, const std::string& text) {
        if (!font || text.empty()) {
            return ImVec2(0.0f, 0.0f);
        }

        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    }

    void DrawShadowedText(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& pos, ImU32 color, const std::string& text) {
        if (!drawList || !font || text.empty()) {
            return;
        }

        drawList->AddText(font, fontSize, ImVec2(pos.x + kNametagShadowOffset, pos.y + kNametagShadowOffset), IM_COL32(0, 0, 0, 175), text.c_str());
        drawList->AddText(font, fontSize, pos, color, text.c_str());
    }

    void DrawShadowedIcon(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& pos, ImU32 color, const char* iconText) {
        if (!drawList || !font || !iconText || !iconText[0]) {
            return;
        }

        drawList->AddText(font, fontSize, ImVec2(pos.x + kNametagShadowOffset, pos.y + kNametagShadowOffset), IM_COL32(0, 0, 0, 175), iconText);
        drawList->AddText(font, fontSize, pos, color, iconText);
    }
}

ImFont* Nametags::s_SanFranciscoBoldFont = nullptr;

void Nametags::SetFont(ImFont* font) {
    s_SanFranciscoBoldFont = font;
}

void Nametags::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (!env) {
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        RestoreVanillaNameTags(env, nullptr);
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    jobject scoreboardObject = world->GetScoreboard(env);
    if (!scoreboardObject) {
        env->DeleteLocalRef(worldObject);
        RestoreVanillaNameTags(env, nullptr);
        return;
    }

    auto* scoreboard = reinterpret_cast<Scoreboard*>(scoreboardObject);
    if (!IsEnabled()) {
        RestoreVanillaNameTags(env, scoreboard);
        env->DeleteLocalRef(scoreboardObject);
        env->DeleteLocalRef(worldObject);
        return;
    }

    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    if (!localPlayerObject) {
        RestoreVanillaNameTags(env, scoreboard);
        env->DeleteLocalRef(scoreboardObject);
        env->DeleteLocalRef(worldObject);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    bool shouldRefreshHealthCache = false;
    bool shouldRefreshVanillaHide = false;
    {
        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        shouldRefreshHealthCache =
            g_LastHealthCacheRefresh == std::chrono::steady_clock::time_point::min() ||
            (now - g_LastHealthCacheRefresh) >= kHealthCacheRefreshInterval;
        shouldRefreshVanillaHide =
            g_LastVanillaHideRefresh == std::chrono::steady_clock::time_point::min() ||
            (now - g_LastVanillaHideRefresh) >= kVanillaHideRefreshInterval;
    }

    if (!shouldRefreshHealthCache && !shouldRefreshVanillaHide) {
        env->DeleteLocalRef(localPlayerObject);
        env->DeleteLocalRef(scoreboardObject);
        env->DeleteLocalRef(worldObject);
        return;
    }

    jobject neverVisibility = nullptr;
    jobject hiddenTeamObject = nullptr;
    if (shouldRefreshVanillaHide) {
        neverVisibility = GetNeverVisibilityEnum(env);
        hiddenTeamObject = scoreboard->GetTeam(env, kHiddenNametagTeamName);
        if (!hiddenTeamObject) {
            hiddenTeamObject = scoreboard->CreateTeam(env, kHiddenNametagTeamName);
        }
        if (hiddenTeamObject && neverVisibility) {
            reinterpret_cast<Team*>(hiddenTeamObject)->SetNameTagVisibility(env, neverVisibility);
        }
    }

    std::set<std::string> activeTeamNames;
    std::unordered_set<std::string> activeInjectedPlayers;
    std::unordered_set<std::string> activePlayerNames;
    const auto players = world->GetPlayerEntities(env);
    for (auto* player : players) {
        if (!player) {
            continue;
        }

        jobject playerObject = reinterpret_cast<jobject>(player);
        if (env->IsSameObject(playerObject, localPlayerObject)) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        if (playerName.empty()) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        activePlayerNames.insert(playerName);

        if (shouldRefreshHealthCache) {
            const float realHealth = player->GetRealHealth(env);
            float maxHealth = player->GetMaxHealth(env);
            if (maxHealth <= 0.0f) {
                maxHealth = (std::max)(20.0f, realHealth);
            } else if (realHealth > maxHealth) {
                maxHealth = realHealth;
            }

            UpdateCachedNametagStats(playerName, realHealth, maxHealth);
        }

        if (shouldRefreshVanillaHide) {
            player->SetAlwaysRenderNameTag(false, env);

            jobject playerTeamObject = scoreboard->GetPlayersTeam(env, playerName);
            if (playerTeamObject && neverVisibility) {
                auto* playerTeam = reinterpret_cast<Team*>(playerTeamObject);
                const std::string teamName = playerTeam->GetRegisteredName(env);
                EnsureTeamNameHidden(env, scoreboard, teamName, playerTeam, neverVisibility, activeTeamNames);
                env->DeleteLocalRef(playerTeamObject);
            } else if (hiddenTeamObject) {
                scoreboard->AddPlayerToTeam(env, playerName, kHiddenNametagTeamName);
                activeInjectedPlayers.insert(playerName);
                activeTeamNames.insert(kHiddenNametagTeamName);
            }
        }

        env->DeleteLocalRef(playerObject);
    }

    if (shouldRefreshHealthCache) {
        CleanupNametagStatsCache(activePlayerNames);
        std::lock_guard<std::mutex> lock(g_NametagStateMutex);
        g_LastHealthCacheRefresh = now;
    }

    if (shouldRefreshVanillaHide) {
        {
            std::lock_guard<std::mutex> lock(g_NametagStateMutex);
            for (auto it = g_InjectedPlayers.begin(); it != g_InjectedPlayers.end();) {
                if (activeInjectedPlayers.count(*it) > 0) {
                    ++it;
                    continue;
                }

                scoreboard->RemovePlayerFromTeams(env, *it);
                it = g_InjectedPlayers.erase(it);
            }

            g_InjectedPlayers.insert(activeInjectedPlayers.begin(), activeInjectedPlayers.end());
            g_LastVanillaHideRefresh = now;
        }

        CleanupInactiveTeamOverrides(env, scoreboard, activeTeamNames);

        if (hiddenTeamObject) {
            env->DeleteLocalRef(hiddenTeamObject);
        }
        if (neverVisibility) {
            env->DeleteLocalRef(neverVisibility);
        }
    }

    env->DeleteLocalRef(localPlayerObject);
    env->DeleteLocalRef(scoreboardObject);
    env->DeleteLocalRef(worldObject);
}

void Nametags::RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
    if (!IsEnabled() || !drawList || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    JNIEnv* env = g_Game->GetCurrentEnv();
    if (!env || env->PushLocalFrame(256) != 0) {
        return;
    }

    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    jobject worldObject = Minecraft::GetTheWorld(env);
    jobject timerObject = Minecraft::GetTimer(env);
    if (!localPlayerObject || !worldObject || !timerObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* localPlayer = reinterpret_cast<Player*>(localPlayerObject);
    auto* world = reinterpret_cast<World*>(worldObject);
    auto* timer = reinterpret_cast<Timer*>(timerObject);

    const RenderMatrixSnapshot renderSnapshot = CaptureRenderMatrixSnapshot();
    if (!renderSnapshot.IsValid()) {
        env->PopLocalFrame(nullptr);
        return;
    }

    const float partialTicks = timer->GetRenderPartialTicks(env);
    std::vector<NametagRenderEntry> entries;
    entries.reserve(32);

    const auto players = world->GetPlayerEntities(env);
    for (auto* player : players) {
        if (!player) {
            continue;
        }

        jobject playerObject = reinterpret_cast<jobject>(player);
        if (env->IsSameObject(playerObject, localPlayerObject)) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        if (player->IsInvisible(env)) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        float realHealth = 0.0f;
        float maxHealth = 20.0f;
        if (playerName.empty() || !GetCachedNametagStats(playerName, realHealth, maxHealth) || realHealth <= 0.0f) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        const float distance = localPlayer->GetDistanceToEntity(playerObject, env);
        if (!std::isfinite(distance) || distance <= 0.0f || distance > kMaximumRenderableDistance) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        float entityHeight = player->GetHeight(env);
        if (entityHeight <= 0.05f) {
            entityHeight = 1.8f;
        }

        const Vec3D position = player->GetPos(env);
        const Vec3D lastPosition = player->GetLastTickPos(env);
        const double interpolatedX = lastPosition.x + (position.x - lastPosition.x) * partialTicks;
        const double interpolatedY = lastPosition.y + (position.y - lastPosition.y) * partialTicks;
        const double interpolatedZ = lastPosition.z + (position.z - lastPosition.z) * partialTicks;

        float entityWidth = player->GetWidth(env);
        if (entityWidth <= 0.05f) {
            entityWidth = 0.6f;
        }

        ProjectedNametagBox projectedBox;
        if (!TryBuildProjectedNametagBox(
                interpolatedX,
                interpolatedY + kNametagVerticalOffset,
                interpolatedZ,
                entityWidth,
                entityHeight,
                renderSnapshot,
                projectedBox)) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        const ScreenPoint anchor {
            std::round((projectedBox.minX + projectedBox.maxX) * 0.5f),
            std::round(projectedBox.minY)
        };

        if (anchor.x < -64.0f || anchor.y < -64.0f || anchor.x > (screenW + 64.0f) || anchor.y > (screenH + 64.0f)) {
            env->DeleteLocalRef(playerObject);
            continue;
        }

        entries.push_back({
            playerName,
            realHealth,
            maxHealth,
            distance,
            anchor,
            ComputeNametagScale(distance)
        });

        env->DeleteLocalRef(playerObject);
    }

    if (entries.empty()) {
        env->PopLocalFrame(nullptr);
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const NametagRenderEntry& left, const NametagRenderEntry& right) {
        return left.distance > right.distance;
    });

    ImDrawList* backgroundDrawList = ImGui::GetBackgroundDrawList();
    if (!backgroundDrawList) {
        backgroundDrawList = drawList;
    }

    ImFont* tagFont = s_SanFranciscoBoldFont ? s_SanFranciscoBoldFont : ImGui::GetFont();
    bool renderedAnyNametag = false;
    std::unordered_set<std::string> activeRenderedNames;

    for (const auto& entry : entries) {
        if (!tagFont) {
            break;
        }

        const auto [smoothedAnchor, smoothedScale] = SmoothNametagState(entry.name, entry.anchor, entry.scale);
        activeRenderedNames.insert(entry.name);

        const float fontSize = kNametagBaseFontSize * smoothedScale;
        const float iconFontSize = fontSize * 0.95f;

        const std::string healthText = FormatHealthText(entry.realHealth);
        const std::string distanceText = FormatDistanceText(entry.distance);

        const ImVec2 nameSize = CalcTextSize(tagFont, fontSize, entry.name);
        const ImVec2 healthSize = CalcTextSize(tagFont, fontSize, healthText);
        const ImVec2 heartSize = CalcTextSize(tagFont, iconFontSize, ICON_MD_FAVORITE);
        const ImVec2 distanceSize = CalcTextSize(tagFont, fontSize, distanceText);

        const float scaledSpacing = kNametagSegmentSpacing * smoothedScale;
        const float totalTextWidth =
            nameSize.x +
            scaledSpacing +
            healthSize.x +
            scaledSpacing +
            heartSize.x +
            scaledSpacing +
            distanceSize.x;

        const float tagWidth = totalTextWidth + (kNametagHorizontalPadding * 2.0f * smoothedScale);
        const float tagHeight = (std::max)({ nameSize.y, healthSize.y, heartSize.y, distanceSize.y }) + (kNametagVerticalPadding * 2.0f * smoothedScale);
        const ImVec2 tagMin(
            std::round(smoothedAnchor.x - (tagWidth * 0.5f)),
            std::round(smoothedAnchor.y - tagHeight - (kNametagTopOffset * smoothedScale)));
        const ImVec2 tagMax(
            std::round(tagMin.x + tagWidth),
            std::round(tagMin.y + tagHeight));

        backgroundDrawList->AddRectFilled(
            tagMin,
            tagMax,
            IM_COL32(12, 12, 12, 172),
            kNametagCornerRounding * smoothedScale);
        backgroundDrawList->AddRect(
            tagMin,
            tagMax,
            IM_COL32(255, 255, 255, 34),
            kNametagCornerRounding * smoothedScale,
            0,
            1.0f);

        const float textBaselineY = std::round(tagMin.y + (tagHeight - nameSize.y) * 0.5f);
        float cursorX = std::round(tagMin.x + (kNametagHorizontalPadding * smoothedScale));

        DrawShadowedText(backgroundDrawList, tagFont, fontSize, ImVec2(cursorX, textBaselineY), IM_COL32(248, 248, 248, 255), entry.name);
        cursorX += nameSize.x + scaledSpacing;

        const ImU32 healthColor = BuildHealthColor(entry.realHealth, entry.maxHealth);
        DrawShadowedText(backgroundDrawList, tagFont, fontSize, ImVec2(cursorX, textBaselineY), healthColor, healthText);
        cursorX += healthSize.x + scaledSpacing;

        const float iconY = std::round(tagMin.y + (tagHeight - heartSize.y) * 0.5f) + (kHeartVerticalOffset * smoothedScale);
        DrawShadowedIcon(backgroundDrawList, tagFont, iconFontSize, ImVec2(cursorX, iconY), IM_COL32(255, 72, 72, 255), ICON_MD_FAVORITE);
        cursorX += heartSize.x + scaledSpacing;

        DrawShadowedText(backgroundDrawList, tagFont, fontSize, ImVec2(cursorX, textBaselineY), IM_COL32(186, 186, 186, 255), distanceText);
        renderedAnyNametag = true;
    }

    CleanupSmoothingCache(activeRenderedNames);

    if (renderedAnyNametag) {
        MarkInUse(120);
    }

    env->PopLocalFrame(nullptr);
}
