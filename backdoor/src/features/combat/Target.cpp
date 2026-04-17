#include "pch.h"
#include "Target.h"

#include "../../core/Bridge.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/MovingObjectPosition.h"
#include "../../game/classes/Scoreboard.h"
#include "../../game/classes/Team.h"
#include "../../game/classes/Timer.h"
#include "../../game/classes/World.h"
#include "../../game/jni/GameInstance.h"

#include <cctype>
#include <cfloat>
#include <gl/GL.h>
#include <limits>

namespace {
    constexpr int kOnlineTargetPlayerLimit = 100;

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

    std::mutex g_BrowseMutex;
    BrowseCacheState g_BrowseCache;

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

    void ClearOnlinePlayersToConfig(ModuleConfig* config) {
        if (!config) {
            return;
        }

        config->Target.m_OnlinePlayersCount = 0;
        memset(config->Target.m_OnlinePlayerNames, 0, sizeof(config->Target.m_OnlinePlayerNames));
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
        return;
    }

    if (config->Target.m_BrowseClearCacheRequested) {
        ClearBrowseCache();
        config->Target.m_BrowseClearCacheRequested = false;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        ClearOnlinePlayersToConfig(config);
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    SyncOnlinePlayersToConfig(env, world, localPlayerObject, config);

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
        m_HitCount = 0;
        m_PreviousSwingProgressInt = 0;
        m_PreviousPhysicalClick = false;
        ClearInUse();
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
    const auto now = std::chrono::steady_clock::now();
    const bool isClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const int swingProgress = localPlayer->GetSwingProgressInt(env);
    const bool newObservedHit = (swingProgress == 1 && m_PreviousSwingProgressInt != 1) || (isClicking && !m_PreviousPhysicalClick);

    auto tryRegisterObservedHit = [&]() -> bool {
        if (!newObservedHit) {
            return false;
        }

        jobject mouseOverObject = Minecraft::GetObjectMouseOver(env);
        if (!mouseOverObject) {
            return false;
        }

        bool browseActivated = false;
        auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject);
        if (mouseOver->IsAimingEntity(env)) {
            jobject attackedObject = mouseOver->GetEntity(env);
            if (attackedObject && !env->IsSameObject(attackedObject, localPlayerObject)) {
                OnEntityAttacked(env, reinterpret_cast<Player*>(attackedObject));
                browseActivated = GetBrowseDisplayInfo().active;
            }
            if (attackedObject) {
                env->DeleteLocalRef(attackedObject);
            }
        }

        env->DeleteLocalRef(mouseOverObject);
        return browseActivated;
    };

    if (config->Target.m_AutoTarget) {
        if (GetLockedTarget().empty()) {
            AutoSelectTarget(env);
        }
        ManageHitboxes(env, localPlayer, world);
    } else if (config->Target.m_TargetSwitch) {
        bool browseActive = false;
        if (config->Target.m_BrowseAllPlayers) {
            std::lock_guard<std::mutex> lock(g_BrowseMutex);
            browseActive = g_BrowseCache.active;
        }

        if (config->Target.m_BrowseAllPlayers && !browseActive && HasLockedTarget()) {
            ClearLockedTarget();
        }

        if (config->Target.m_BrowseAllPlayers && !browseActive && tryRegisterObservedHit()) {
            browseActive = true;
            if (!HasLockedTarget()) {
                SwitchToNextTarget(env);
                m_LastSwitchTime = now;
                m_TargetLockedTime = now;
                m_HitCount = 0;
            }
        }

        if (!HasLockedTarget()) {
            if (!config->Target.m_BrowseAllPlayers || browseActive) {
                SwitchToNextTarget(env);
                m_LastSwitchTime = now;
                m_TargetLockedTime = now;
                m_HitCount = 0;
            } else {
                m_HitCount = 0;
            }
        } else {
            if (newObservedHit) {
                jobject mouseOverObject = Minecraft::GetObjectMouseOver(env);
                if (mouseOverObject) {
                    auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject);
                    if (mouseOver->IsAimingEntity(env)) {
                        jobject attackedObject = mouseOver->GetEntity(env);
                        if (attackedObject && !env->IsSameObject(attackedObject, localPlayerObject)) {
                            OnEntityAttacked(env, reinterpret_cast<Player*>(attackedObject));
                        }
                        if (attackedObject) {
                            env->DeleteLocalRef(attackedObject);
                        }
                    }
                    env->DeleteLocalRef(mouseOverObject);
                }

                if (config->Target.m_SwitchMode == 0) {
                    ++m_HitCount;
                }
            }

            bool shouldSwitch = IsCurrentTargetInvalid(env, localPlayer);
            if (!shouldSwitch) {
                if (config->Target.m_SwitchMode == 0) {
                    shouldSwitch = m_HitCount >= config->Target.m_SwitchHits;
                } else {
                    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_TargetLockedTime).count();
                    shouldSwitch = elapsedMs >= config->Target.m_SwitchTimeMs;
                }
            }

            if (shouldSwitch) {
                const auto cooldownMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastSwitchTime).count();
                if (cooldownMs >= kSwitchCooldownMs) {
                    SwitchToNextTarget(env);
                    m_LastSwitchTime = now;
                    m_TargetLockedTime = now;
                    m_HitCount = 0;
                }
            }
        }

        ManageHitboxes(env, localPlayer, world);
    } else {
        const std::string targetName = config->Target.m_PlayerName;
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
            if (!targetName.empty() && playerName == targetName) {
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

    if (config->Target.m_AutoTarget || config->Target.m_TargetSwitch) {
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

    m_PreviousPhysicalClick = isClicking;
    m_PreviousSwingProgressInt = swingProgress;
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
    const bool browseWaitingMode = config && config->Target.m_TargetSwitch && config->Target.m_BrowseAllPlayers;

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
            if (playerName == lockedTarget) {
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
        if (score < bestScore) {
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
    Player* previousFallback = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    float fallbackDistance = std::numeric_limits<float>::max();
    const auto players = world->GetPlayerEntities(env);

    for (auto* player : players) {
        if (!player) {
            continue;
        }

        if (env->IsSameObject(reinterpret_cast<jobject>(player), reinterpret_cast<jobject>(localPlayer))) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        if (!MatchesBrowseCache(env, player, scoreboard, cacheSnapshot) || !IsValidCombatTarget(env, player, localPlayer)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        if (playerName.empty() || cacheSnapshot.processed.count(playerName) > 0) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const float distance = localPlayer->GetDistanceToEntity(reinterpret_cast<jobject>(player), env);
        if (!previousTarget.empty() && playerName == previousTarget) {
            if (distance < fallbackDistance) {
                if (previousFallback) {
                    env->DeleteLocalRef(reinterpret_cast<jobject>(previousFallback));
                }
                previousFallback = player;
                fallbackDistance = distance;
            } else {
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            }
            continue;
        }

        if (distance < bestDistance) {
            if (bestTarget) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
            }
            bestTarget = player;
            bestDistance = distance;
        } else {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }
    }

    Player* selectedTarget = bestTarget ? bestTarget : previousFallback;
    if (!selectedTarget) {
        return false;
    }

    nextTarget = selectedTarget->GetName(env, true);
    if (selectedTarget != bestTarget && bestTarget) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
    }
    if (selectedTarget != previousFallback && previousFallback) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(previousFallback));
    }
    env->DeleteLocalRef(reinterpret_cast<jobject>(selectedTarget));
    return !nextTarget.empty();
}

void Target::SwitchToNextTarget(JNIEnv* env) {
    auto* config = Bridge::Get()->GetConfig();
    const std::string previousTarget = GetLockedTarget();
    if (config && config->Target.m_BrowseAllPlayers) {
        MarkBrowsePlayerProcessed(previousTarget);
    }
    ClearLockedTarget();

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

    const BrowseDisplayInfo browseInfo = GetBrowseDisplayInfo();
    if (config && config->Target.m_BrowseAllPlayers && browseInfo.active) {
        std::string nextBrowseTarget;
        if (TrySelectBrowseTarget(env, localPlayer, world, scoreboard, previousTarget, nextBrowseTarget) && !nextBrowseTarget.empty()) {
            SetLockedTarget(nextBrowseTarget);
        }
        if (scoreboard) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
        }
        env->DeleteLocalRef(localPlayerObject);
        env->DeleteLocalRef(worldObject);
        return;
    }

    const auto players = world->GetPlayerEntities(env);
    Player* bestTarget = nullptr;
    float bestDistance = std::numeric_limits<float>::max();

    for (auto* player : players) {
        if (!player) {
            continue;
        }

        if (env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        if (!IsValidCombatTarget(env, player, localPlayer) || IsSameClanLikeHideClans(env, player, localPlayer, scoreboard)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        if (!previousTarget.empty() && playerName == previousTarget) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const float distance = localPlayer->GetDistanceToEntity(reinterpret_cast<jobject>(player), env);
        if (distance < bestDistance) {
            if (bestTarget) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
            }
            bestTarget = player;
            bestDistance = distance;
        } else {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }
    }

    if (bestTarget) {
        SetLockedTarget(bestTarget->GetName(env, true));
        env->DeleteLocalRef(reinterpret_cast<jobject>(bestTarget));
    } else if (!previousTarget.empty()) {
        SetLockedTarget(previousTarget);
    }

    if (scoreboard) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
    }
    env->DeleteLocalRef(localPlayerObject);
    env->DeleteLocalRef(worldObject);
}

bool Target::IsCurrentTargetInvalid(JNIEnv* env, Player* localPlayer) {
    const std::string currentTarget = GetLockedTarget();
    if (currentTarget.empty()) {
        return true;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        return true;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    const auto players = world->GetPlayerEntities(env);
    bool found = false;

    for (auto* player : players) {
        if (!player) {
            continue;
        }

        if (env->IsSameObject(reinterpret_cast<jobject>(player), reinterpret_cast<jobject>(localPlayer))) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        if (playerName == currentTarget) {
            found = IsValidCombatTarget(env, player, localPlayer);
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            break;
        }

        env->DeleteLocalRef(reinterpret_cast<jobject>(player));
    }

    env->DeleteLocalRef(worldObject);
    return !found;
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
    const float armorVulnerability = GetArmorVulnerability(env, player);
    const float distance = localPlayer->GetDistanceToEntity(reinterpret_cast<jobject>(player), env);

    switch (config->Target.m_PriorityMode) {
    case 1:
        return (distance * 0.5f) - (armorVulnerability * 2.0f);
    case 2:
        return (health * config->Target.m_BothHealthWeight) +
            (distance * 0.5f) -
            (armorVulnerability * config->Target.m_BothArmorWeight * 2.0f);
    case 0:
    default:
        return health + (distance * 0.5f);
    }
}

float Target::GetArmorVulnerability(JNIEnv* env, Player* player) {
    auto* config = Bridge::Get()->GetConfig();
    if (!config) {
        return 0.0f;
    }

    float vulnerability = 0.0f;
    int missingPieces = 0;
    for (int slot = 0; slot < 4; ++slot) {
        jobject armorObject = player->GetCurrentArmor(slot, env);
        if (armorObject) {
            vulnerability += 5.0f;
            env->DeleteLocalRef(armorObject);
        } else {
            ++missingPieces;
            vulnerability += 5.0f;
        }
    }

    vulnerability *= (1.0f + (config->Target.m_BrokenArmorPriority * 0.1f));
    vulnerability += missingPieces * 3.0f;
    return vulnerability;
}

bool Target::IsSameClan(JNIEnv* env, Player* player, Player* localPlayer, Scoreboard* scoreboard) {
    return IsSameClanLikeHideClans(env, player, localPlayer, scoreboard);
}
