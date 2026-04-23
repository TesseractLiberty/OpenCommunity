#include "pch.h"
#include "EnemyInfoList.h"

#include "../../core/Bridge.h"
#include "../../game/classes/AxisAlignedBB.h"
#include "../../game/classes/ItemArmor.h"
#include "../../game/classes/ItemStack.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/MovingObjectPosition.h"
#include "../../game/classes/Scoreboard.h"
#include "../../game/classes/Team.h"
#include "../../game/classes/World.h"
#include "../../game/jni/Class.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/mapping/Mapper.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
    constexpr int kEnemyInfoListMaxEntries = 15;
    constexpr int kArmorPieceCount = 4;

    struct EnemyArmorSnapshot {
        int remainingDurability[kArmorPieceCount] = {};
        int maxDurability[kArmorPieceCount] = {};
        bool hasArmor[kArmorPieceCount] = {};
        bool inWeb = false;
        bool inRender = false;
    };

    struct EnemyInfoState {
        bool active = false;
        std::string clanTagDisplay;
        std::string plainClanTag;
        std::string normalizedClanTag;
        std::string registeredTeamName;
        std::string trackedTargetName;
        std::unordered_set<std::string> members;
        std::unordered_map<std::string, EnemyArmorSnapshot> snapshots;
    };

    std::mutex g_EnemyInfoMutex;
    EnemyInfoState g_EnemyInfoState;

    bool CaseInsensitiveEquals(const std::string& lhs, const std::string& rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (size_t index = 0; index < lhs.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
                std::tolower(static_cast<unsigned char>(rhs[index]))) {
                return false;
            }
        }

        return true;
    }

    bool CaseInsensitiveLess(const std::string& lhs, const std::string& rhs) {
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

    std::string NormalizeClanTag(const std::string& tag) {
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

    void ClearEnemyInfoEntries(ModuleConfig* config) {
        if (!config) {
            return;
        }

        config->EnemyInfoList.m_EntryCount = 0;
        memset(config->EnemyInfoList.m_TrackedClan, 0, sizeof(config->EnemyInfoList.m_TrackedClan));
        memset(config->EnemyInfoList.m_TrackedClanFormatted, 0, sizeof(config->EnemyInfoList.m_TrackedClanFormatted));
        memset(config->EnemyInfoList.m_TrackedTargetName, 0, sizeof(config->EnemyInfoList.m_TrackedTargetName));
        memset(config->EnemyInfoList.m_Entries, 0, sizeof(config->EnemyInfoList.m_Entries));
    }

    void SyncEnemyInfoToConfig(const EnemyInfoState& snapshot) {
        auto* config = Bridge::Get()->GetConfig();
        if (!config) {
            return;
        }

        ClearEnemyInfoEntries(config);
        if (!snapshot.active) {
            return;
        }

        strncpy_s(
            config->EnemyInfoList.m_TrackedClan,
            snapshot.plainClanTag.empty() ? snapshot.registeredTeamName.c_str() : snapshot.plainClanTag.c_str(),
            _TRUNCATE);
        strncpy_s(config->EnemyInfoList.m_TrackedClanFormatted, snapshot.clanTagDisplay.c_str(), _TRUNCATE);
        strncpy_s(config->EnemyInfoList.m_TrackedTargetName, snapshot.trackedTargetName.c_str(), _TRUNCATE);

        std::vector<std::string> orderedMembers(snapshot.members.begin(), snapshot.members.end());
        std::sort(orderedMembers.begin(), orderedMembers.end(), CaseInsensitiveLess);

        int entryCount = 0;
        for (const auto& playerName : orderedMembers) {
            if (entryCount >= kEnemyInfoListMaxEntries) {
                break;
            }

            const auto snapshotIt = snapshot.snapshots.find(playerName);
            if (snapshotIt == snapshot.snapshots.end()) {
                continue;
            }

            auto& destination = config->EnemyInfoList.m_Entries[entryCount];
            const auto& source = snapshotIt->second;
            strncpy_s(destination.m_Name, playerName.c_str(), _TRUNCATE);
            for (int armorIndex = 0; armorIndex < kArmorPieceCount; ++armorIndex) {
                destination.m_RemainingDurability[armorIndex] = source.remainingDurability[armorIndex];
                destination.m_MaxDurability[armorIndex] = source.maxDurability[armorIndex];
                destination.m_HasArmor[armorIndex] = source.hasArmor[armorIndex];
            }
            destination.m_InWeb = source.inWeb;
            destination.m_InRender = source.inRender;
            ++entryCount;
        }

        config->EnemyInfoList.m_EntryCount = entryCount;
    }

    void SyncEnemyInfoToConfig() {
        EnemyInfoState snapshot;
        {
            std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
            snapshot = g_EnemyInfoState;
        }
        SyncEnemyInfoToConfig(snapshot);
    }

    void ClearEnemyInfoState(bool clearConfig = true) {
        {
            std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
            g_EnemyInfoState = {};
        }

        if (clearConfig) {
            auto* config = Bridge::Get()->GetConfig();
            if (config) {
                ClearEnemyInfoEntries(config);
            }
        }
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

    bool TeamMatchesEnemyCache(JNIEnv* env, Team* team, const EnemyInfoState& cache) {
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

        const std::string normalizedPrefix = NormalizeClanTag(StripMinecraftFormatting(team->GetColorPrefix(env)));
        const std::string normalizedSuffix = NormalizeClanTag(StripMinecraftFormatting(team->GetColorSuffix(env)));
        return (!normalizedPrefix.empty() && normalizedPrefix.find(cache.normalizedClanTag) != std::string::npos) ||
            (!normalizedSuffix.empty() && normalizedSuffix.find(cache.normalizedClanTag) != std::string::npos);
    }

    void CollectEnemyMembersFromNetHandler(JNIEnv* env, const EnemyInfoState& cache, std::unordered_set<std::string>& members) {
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
                        TeamMatchesEnemyCache(env, reinterpret_cast<Team*>(playerTeamObject), cache)) {
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

    bool MatchesEnemyCache(JNIEnv* env, Player* player, Scoreboard* scoreboard, const EnemyInfoState& cache) {
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
            const std::string normalizedTag = NormalizeClanTag(player->GetClanTag(env, scoreboard));
            matches = !normalizedTag.empty() && normalizedTag == cache.normalizedClanTag;
        }

        if (playerTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(playerTeam));
        }

        return matches;
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
        const std::string localClanTag = NormalizeClanTag(localPlayer->GetClanTag(env, scoreboard));
        const std::string playerClanTag = NormalizeClanTag(player->GetClanTag(env, scoreboard));

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

    bool IsPlayerTouchingWebBlock(JNIEnv* env, World* world, Player* player) {
        if (!env || !world || !player) {
            return false;
        }

        if (player->IsInWeb(env)) {
            return true;
        }

        jobject boundingBoxObject = player->GetBoundingBox(env);
        AxisAlignedBB_t bounds{};
        if (boundingBoxObject) {
            bounds = reinterpret_cast<AxisAlignedBB*>(boundingBoxObject)->GetNativeBoundingBox(env);
            env->DeleteLocalRef(boundingBoxObject);
        }

        const bool hasBounds =
            bounds.maxX > bounds.minX &&
            bounds.maxY > bounds.minY &&
            bounds.maxZ > bounds.minZ;
        if (!hasBounds) {
            const Vec3D position = player->GetPos(env);
            const float width = (std::max)(0.6f, player->GetWidth(env));
            const float height = (std::max)(1.8f, player->GetHeight(env));
            const double halfWidth = static_cast<double>(width) * 0.5;
            bounds.minX = static_cast<float>(position.x - halfWidth);
            bounds.minY = static_cast<float>(position.y);
            bounds.minZ = static_cast<float>(position.z - halfWidth);
            bounds.maxX = static_cast<float>(position.x + halfWidth);
            bounds.maxY = static_cast<float>(position.y + static_cast<double>(height));
            bounds.maxZ = static_cast<float>(position.z + halfWidth);
        }

        constexpr double kInset = 0.001;
        const int minX = static_cast<int>(std::floor(static_cast<double>(bounds.minX) + kInset));
        const int minY = static_cast<int>(std::floor(static_cast<double>(bounds.minY) + kInset));
        const int minZ = static_cast<int>(std::floor(static_cast<double>(bounds.minZ) + kInset));
        const int maxX = static_cast<int>(std::floor(static_cast<double>(bounds.maxX) - kInset));
        const int maxY = static_cast<int>(std::floor(static_cast<double>(bounds.maxY) - kInset));
        const int maxZ = static_cast<int>(std::floor(static_cast<double>(bounds.maxZ) - kInset));

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    if (world->IsWebBlockAt(env, x, y, z)) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    EnemyArmorSnapshot BuildSnapshotFromPlayer(JNIEnv* env, World* world, Player* player) {
        EnemyArmorSnapshot snapshot;
        if (!env || !player) {
            return snapshot;
        }

        static constexpr std::array<int, 4> kPlayerArmorSlots = { 3, 2, 1, 0 };
        for (size_t armorIndex = 0; armorIndex < kPlayerArmorSlots.size(); ++armorIndex) {
            jobject armorObject = player->GetCurrentArmor(kPlayerArmorSlots[armorIndex], env);
            if (!armorObject) {
                continue;
            }

            auto* armorStack = reinterpret_cast<ItemStack*>(armorObject);
            jobject itemObject = armorStack->GetItem(env);
            const bool isArmor = armorStack->IsArmor(env) && itemObject && ItemArmor::GetArmorType(itemObject, env) >= 0;
            if (itemObject) {
                env->DeleteLocalRef(itemObject);
            }

            if (isArmor) {
                const int maxDamage = armorStack->GetMaxDamage(env);
                const int itemDamage = armorStack->GetItemDamage(env);
                const int safeDamage = (std::max)(0, (std::min)(maxDamage, itemDamage));
                snapshot.maxDurability[armorIndex] = (std::max)(0, maxDamage);
                snapshot.remainingDurability[armorIndex] = (std::max)(0, maxDamage - safeDamage);
                snapshot.hasArmor[armorIndex] = true;
            }

            env->DeleteLocalRef(armorObject);
        }

        snapshot.inWeb = IsPlayerTouchingWebBlock(env, world, player);
        snapshot.inRender = true;
        return snapshot;
    }

    void RefreshEnemyInfoState(JNIEnv* env, World* world, Scoreboard* scoreboard) {
        if (!env || !world || !scoreboard) {
            return;
        }

        EnemyInfoState cacheSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
            cacheSnapshot = g_EnemyInfoState;
        }
        if (!cacheSnapshot.active) {
            return;
        }

        jobject localPlayerObject = Minecraft::GetThePlayer(env);
        std::unordered_set<std::string> discoveredMembers;
        const auto players = world->GetPlayerEntities(env);
        for (auto* player : players) {
            if (!player) {
                continue;
            }

            if (localPlayerObject && env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                continue;
            }

            if (MatchesEnemyCache(env, player, scoreboard, cacheSnapshot)) {
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

        CollectEnemyMembersFromNetHandler(env, cacheSnapshot, discoveredMembers);

        std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
        if (!g_EnemyInfoState.active ||
            g_EnemyInfoState.normalizedClanTag != cacheSnapshot.normalizedClanTag ||
            g_EnemyInfoState.registeredTeamName != cacheSnapshot.registeredTeamName) {
            return;
        }

        g_EnemyInfoState.members = std::move(discoveredMembers);
        for (auto snapshotIt = g_EnemyInfoState.snapshots.begin(); snapshotIt != g_EnemyInfoState.snapshots.end();) {
            if (g_EnemyInfoState.members.count(snapshotIt->first) == 0) {
                snapshotIt = g_EnemyInfoState.snapshots.erase(snapshotIt);
            } else {
                ++snapshotIt;
            }
        }
    }

    void UpdateEnemySnapshots(JNIEnv* env, World* world) {
        if (!env || !world) {
            return;
        }

        EnemyInfoState cacheSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
            cacheSnapshot = g_EnemyInfoState;
        }
        if (!cacheSnapshot.active || cacheSnapshot.members.empty()) {
            return;
        }

        struct WorldPlayerEntry {
            std::string name;
            Player* player = nullptr;
        };

        jobject localPlayerObject = Minecraft::GetThePlayer(env);
        const auto players = world->GetPlayerEntities(env);
        std::vector<WorldPlayerEntry> worldPlayers;
        worldPlayers.reserve(players.size());
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

            worldPlayers.push_back({ playerName, player });
        }

        if (localPlayerObject) {
            env->DeleteLocalRef(localPlayerObject);
        }

        std::unordered_map<std::string, EnemyArmorSnapshot> nextSnapshots = cacheSnapshot.snapshots;
        for (const auto& memberName : cacheSnapshot.members) {
            auto worldPlayerIt = std::find_if(worldPlayers.begin(), worldPlayers.end(), [&](const WorldPlayerEntry& entry) {
                return CaseInsensitiveEquals(entry.name, memberName);
            });

            if (worldPlayerIt != worldPlayers.end() && worldPlayerIt->player) {
                nextSnapshots[memberName] = BuildSnapshotFromPlayer(env, world, worldPlayerIt->player);
            } else {
                auto snapshotIt = nextSnapshots.find(memberName);
                if (snapshotIt != nextSnapshots.end()) {
                    snapshotIt->second.inRender = false;
                } else {
                    EnemyArmorSnapshot emptySnapshot;
                    emptySnapshot.inRender = false;
                    nextSnapshots.emplace(memberName, emptySnapshot);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
            if (!g_EnemyInfoState.active ||
                g_EnemyInfoState.normalizedClanTag != cacheSnapshot.normalizedClanTag ||
                g_EnemyInfoState.registeredTeamName != cacheSnapshot.registeredTeamName) {
                for (auto& worldPlayer : worldPlayers) {
                    if (worldPlayer.player) {
                        env->DeleteLocalRef(reinterpret_cast<jobject>(worldPlayer.player));
                    }
                }
                return;
            }

            g_EnemyInfoState.snapshots = std::move(nextSnapshots);
        }

        for (auto& worldPlayer : worldPlayers) {
            if (worldPlayer.player) {
                env->DeleteLocalRef(reinterpret_cast<jobject>(worldPlayer.player));
            }
        }
    }

    void ObserveEnemyInfoAttackFallback(JNIEnv* env, jobject localPlayerObject) {
        if (!env || !localPlayerObject) {
            return;
        }

        jobject mouseOverObject = Minecraft::GetObjectMouseOver(env);
        if (!mouseOverObject) {
            return;
        }

        auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject);
        if (mouseOver->IsAimingEntity(env)) {
            jobject attackedObject = mouseOver->GetEntity(env);
            if (attackedObject && !env->IsSameObject(attackedObject, localPlayerObject)) {
                bool isPlayer = true;
                if (g_Game && g_Game->IsInitialized()) {
                    Class* playerClass = g_Game->FindClass(Mapper::Get("net/minecraft/entity/player/EntityPlayer"));
                    isPlayer = playerClass && env->IsInstanceOf(attackedObject, reinterpret_cast<jclass>(playerClass));
                }

                if (isPlayer) {
                    EnemyInfoList::OnLocalAttack(env, reinterpret_cast<Player*>(attackedObject));
                }
            }

            if (attackedObject) {
                env->DeleteLocalRef(attackedObject);
            }
        }

        env->DeleteLocalRef(mouseOverObject);
    }
}

void EnemyInfoList::OnLocalAttack(JNIEnv* env, Player* attackedPlayer) {
    auto* config = Bridge::Get()->GetConfig();
    if (!env || !attackedPlayer || !config || !config->EnemyInfoList.m_Enabled) {
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
    const std::string normalizedClanTag = NormalizeClanTag(plainClanTag);

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

    EnemyInfoState nextState;
    nextState.active = true;
    nextState.clanTagDisplay = !formattedClanTag.empty()
        ? formattedClanTag
        : (!plainClanTag.empty() ? plainClanTag : registeredTeamName);
    nextState.plainClanTag = plainClanTag;
    nextState.normalizedClanTag = normalizedClanTag;
    nextState.registeredTeamName = registeredTeamName;
    nextState.trackedTargetName = attackedName;

    {
        std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
        const bool sameGroup =
            g_EnemyInfoState.active &&
            g_EnemyInfoState.normalizedClanTag == normalizedClanTag &&
            g_EnemyInfoState.registeredTeamName == registeredTeamName;
        if (sameGroup) {
            nextState.snapshots = g_EnemyInfoState.snapshots;
        }
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
            const std::string normalizedPlayerTag = NormalizeClanTag(player->GetClanTag(env, scoreboard));
            matches = !normalizedPlayerTag.empty() && normalizedPlayerTag == normalizedClanTag;
        }

        if (playerTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(playerTeam));
        }

        if (matches) {
            nextState.members.insert(playerName);
            nextState.snapshots[playerName] = BuildSnapshotFromPlayer(env, world, player);
        }

        env->DeleteLocalRef(reinterpret_cast<jobject>(player));
    }

    if (!attackedName.empty()) {
        nextState.members.insert(attackedName);
    }
    CollectEnemyMembersFromNetHandler(env, nextState, nextState.members);

    {
        std::lock_guard<std::mutex> lock(g_EnemyInfoMutex);
        g_EnemyInfoState = std::move(nextState);
    }
    SyncEnemyInfoToConfig();

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

void EnemyInfoList::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    auto* config = Bridge::Get()->GetConfig();
    if (!config) {
        return;
    }

    if (!env) {
        ClearEnemyInfoEntries(config);
        m_PreviousSwingProgressInt = 0;
        m_PreviousPhysicalClick = false;
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        ClearEnemyInfoEntries(config);
        m_PreviousSwingProgressInt = 0;
        m_PreviousPhysicalClick = false;
        return;
    }

    auto* world = reinterpret_cast<World*>(worldObject);
    auto* scoreboard = reinterpret_cast<Scoreboard*>(world->GetScoreboard(env));

    if (!IsEnabled()) {
        if (m_WasEnabled) {
            ClearEnemyInfoState(true);
            m_WasEnabled = false;
        } else {
            ClearEnemyInfoEntries(config);
        }
        m_PreviousSwingProgressInt = 0;
        m_PreviousPhysicalClick = false;

        if (scoreboard) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
        }
        env->DeleteLocalRef(worldObject);
        return;
    }

    m_WasEnabled = true;

    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    if (localPlayerObject) {
        auto* localPlayer = reinterpret_cast<Player*>(localPlayerObject);
        const bool isClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const int swingProgress = localPlayer->GetSwingProgressInt(env);
        const bool newObservedHit =
            (swingProgress == 1 && m_PreviousSwingProgressInt != 1) ||
            (isClicking && !m_PreviousPhysicalClick);

        if (newObservedHit) {
            ObserveEnemyInfoAttackFallback(env, localPlayerObject);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }

        m_PreviousPhysicalClick = isClicking;
        m_PreviousSwingProgressInt = swingProgress;
        env->DeleteLocalRef(localPlayerObject);
    } else {
        m_PreviousSwingProgressInt = 0;
        m_PreviousPhysicalClick = false;
    }

    static auto lastRefreshTime = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const bool shouldRefresh =
        lastRefreshTime.time_since_epoch().count() == 0 ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefreshTime).count() >= 125;

    if (shouldRefresh) {
        lastRefreshTime = now;
        RefreshEnemyInfoState(env, world, scoreboard);
        UpdateEnemySnapshots(env, world);
    }

    SyncEnemyInfoToConfig();

    if (scoreboard) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
    }
    env->DeleteLocalRef(worldObject);
}

void EnemyInfoList::ShutdownRuntime(void* envPtr) {
    (void)envPtr;
    ClearEnemyInfoState(true);
    m_PreviousSwingProgressInt = 0;
    m_PreviousPhysicalClick = false;
    m_WasEnabled = false;
}

std::string EnemyInfoList::GetTag() const {
    auto* config = Bridge::Get()->GetConfig();
    if (!config || !config->EnemyInfoList.m_Enabled) {
        return {};
    }

    if (config->EnemyInfoList.m_TrackedClanFormatted[0] != '\0') {
        return config->EnemyInfoList.m_TrackedClanFormatted;
    }
    if (config->EnemyInfoList.m_TrackedClan[0] != '\0') {
        return config->EnemyInfoList.m_TrackedClan;
    }
    return {};
}
