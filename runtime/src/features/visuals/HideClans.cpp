#include "pch.h"
#include "HideClans.h"

#include "Target.h"
#include "../../core/Bridge.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/classes/Player.h"
#include "../../game/classes/Scoreboard.h"
#include "../../game/classes/Team.h"
#include "../../game/classes/World.h"

#include <cctype>
#include <set>

namespace {
    std::mutex g_TagMutex;
    std::string g_CachedClanTag;

    std::string NormalizeTag(std::string tag) {
        std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        tag.erase(std::remove_if(tag.begin(), tag.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }), tag.end());
        return tag;
    }

    bool IsAllyColor(const std::string& formattedName) {
        if (formattedName.empty()) {
            return false;
        }

        const bool hasGreen = formattedName.find("\247a") != std::string::npos || formattedName.find("\2472") != std::string::npos;
        const bool hasRed = formattedName.find("\247c") != std::string::npos || formattedName.find("\2474") != std::string::npos;
        return hasGreen && !hasRed;
    }

    bool g_SemiAutoLocked = false;
    std::set<std::string> g_SemiAutoLockedPlayers;
    bool g_WasShowAlliesEnabled = false;
    bool g_WasEnabled = false;
}

std::string HideClans::GetCachedClanTag() {
    std::string clanTag;
    {
        std::lock_guard<std::mutex> lock(g_TagMutex);
        clanTag = g_CachedClanTag;
    }
    return clanTag;
}

void HideClans::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    auto* config = Bridge::Get()->GetConfig();
    if (!env || !config) {
        return;
    }

    static auto lastUpdate = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const bool shouldUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count() > 100;

    if (config->Target.m_Enabled || Target::IsTargetActivelyManaging()) {
        if (g_WasEnabled) {
            jobject worldObject = Minecraft::GetTheWorld(env);
            if (worldObject) {
                auto* world = reinterpret_cast<World*>(worldObject);
                const auto players = world->GetPlayerEntities(env);
                for (auto* player : players) {
                    if (player) {
                        try { player->Restore(env); } catch (...) {}
                        env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                    }
                }
                env->DeleteLocalRef(worldObject);
            }
            g_WasEnabled = false;
        }
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        return;
    }
    auto* world = reinterpret_cast<World*>(worldObject);

    if (!IsEnabled()) {
        if (g_WasEnabled) {
            const auto players = world->GetPlayerEntities(env);
            for (auto* player : players) {
                if (player) {
                    try { player->Restore(env); } catch (...) {}
                    env->DeleteLocalRef(reinterpret_cast<jobject>(player));
                }
            }
            g_WasEnabled = false;
            g_SemiAutoLocked = false;
            g_SemiAutoLockedPlayers.clear();
        }
        ClearInUse();
        env->DeleteLocalRef(worldObject);
        return;
    }
    g_WasEnabled = true;

    const bool showAlliesEnabled = config->HideClans.m_ShowAllies;
    if (!showAlliesEnabled && g_WasShowAlliesEnabled) {
        g_SemiAutoLocked = false;
        g_SemiAutoLockedPlayers.clear();
    }
    g_WasShowAlliesEnabled = showAlliesEnabled;

    if (!shouldUpdate) {
        env->DeleteLocalRef(worldObject);
        return;
    }
    lastUpdate = now;

    jobject localPlayerObject = Minecraft::GetThePlayer(env);
    if (!localPlayerObject) {
        env->DeleteLocalRef(worldObject);
        return;
    }

    auto* localPlayer = reinterpret_cast<Player*>(localPlayerObject);
    auto* scoreboard = reinterpret_cast<Scoreboard*>(world->GetScoreboard(env));

    const std::string localName = localPlayer->GetName(env, true);
    const std::string localFormatted = localPlayer->GetName(env, false);
    const std::string localClanTag = NormalizeTag(localPlayer->GetClanTag(env, scoreboard));
    const std::string localDisplayTag = localPlayer->GetFormattedClanTag(env, scoreboard);
    const bool localIsAllyColor = IsAllyColor(localFormatted);
    auto* localTeam = scoreboard ? reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, localName)) : nullptr;
    const std::string localTeamRegisteredName = localTeam ? localTeam->GetRegisteredName(env) : "";
    {
        std::lock_guard<std::mutex> lock(g_TagMutex);
        g_CachedClanTag = localDisplayTag;
    }

    struct AllyInfo {
        Player* player = nullptr;
        std::string name;
        float distance = 9999.0f;
    };

    std::vector<AllyInfo> allies;
    std::vector<Player*> nonAllies;
    const auto players = world->GetPlayerEntities(env);
    for (auto* player : players) {
        if (!player) {
            continue;
        }

        if (env->IsSameObject(reinterpret_cast<jobject>(player), localPlayerObject)) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
            continue;
        }

        const std::string playerName = player->GetName(env, true);
        const std::string playerFormatted = player->GetName(env, false);
        auto* playerTeam = scoreboard ? reinterpret_cast<Team*>(scoreboard->GetPlayersTeam(env, playerName)) : nullptr;

        bool sameTeam = false;
        if (localTeam && playerTeam) {
            const std::string playerTeamRegisteredName = playerTeam->GetRegisteredName(env);
            if (!localTeamRegisteredName.empty() && localTeamRegisteredName == playerTeamRegisteredName) {
                sameTeam = true;
            } else {
                sameTeam = localTeam->IsSameTeam(env, playerTeam);
            }
        }

        if (!sameTeam && !localClanTag.empty()) {
            const std::string playerClanTag = NormalizeTag(player->GetClanTag(env, scoreboard));
            sameTeam = !playerClanTag.empty() && playerClanTag == localClanTag;
        }

        if (!sameTeam && localIsAllyColor && IsAllyColor(playerFormatted)) {
            sameTeam = true;
        }

        if (playerTeam) {
            env->DeleteLocalRef(reinterpret_cast<jobject>(playerTeam));
        }

        if (sameTeam) {
            allies.push_back({ player, playerName, localPlayer->GetDistanceToEntity(reinterpret_cast<jobject>(player), env) });
        } else {
            nonAllies.push_back(player);
        }
    }

    for (auto* player : nonAllies) {
        if (player) {
            try { player->Restore(env); } catch (...) {}
            env->DeleteLocalRef(reinterpret_cast<jobject>(player));
        }
    }

    const bool showAllies = config->HideClans.m_ShowAllies;
    const int showAlliesMode = config->HideClans.m_ShowAlliesMode;
    const int showCount = (std::max)(1, (std::min)(14, config->HideClans.m_ShowAlliesCount));
    bool affectedPlayers = false;

    if (!showAllies) {
        for (auto& ally : allies) {
            if (ally.player) {
                try { ally.player->Zero(env); } catch (...) {}
                env->DeleteLocalRef(reinterpret_cast<jobject>(ally.player));
                affectedPlayers = true;
            }
        }
    } else if (showAlliesMode == 0) {
        std::sort(allies.begin(), allies.end(), [](const AllyInfo& a, const AllyInfo& b) {
            return a.distance < b.distance;
        });

        for (int index = 0; index < static_cast<int>(allies.size()); ++index) {
            if (!allies[index].player) {
                continue;
            }

            try {
                if (index < showCount) {
                    allies[index].player->Restore(env);
                } else {
                    allies[index].player->Zero(env);
                    affectedPlayers = true;
                }
            } catch (...) {}
            env->DeleteLocalRef(reinterpret_cast<jobject>(allies[index].player));
        }
    } else if (showAlliesMode == 1) {
        if (!g_SemiAutoLocked) {
            g_SemiAutoLockedPlayers.clear();
            std::sort(allies.begin(), allies.end(), [](const AllyInfo& a, const AllyInfo& b) {
                return a.distance < b.distance;
            });
            for (int index = 0; index < static_cast<int>(allies.size()) && index < showCount; ++index) {
                g_SemiAutoLockedPlayers.insert(allies[index].name);
            }
            g_SemiAutoLocked = true;
        }

        for (auto& ally : allies) {
            if (!ally.player) {
                continue;
            }

            try {
                if (g_SemiAutoLockedPlayers.count(ally.name) > 0) {
                    ally.player->Restore(env);
                } else {
                    ally.player->Zero(env);
                    affectedPlayers = true;
                }
            } catch (...) {}
            env->DeleteLocalRef(reinterpret_cast<jobject>(ally.player));
        }
    } else {
        int count = 0;
        for (auto& ally : allies) {
            if (count >= 50) {
                break;
            }

            int existingIndex = -1;
            for (int index = 0; index < config->HideClans.m_ManualCount; ++index) {
                if (ally.name == config->HideClans.m_ManualNames[index]) {
                    existingIndex = index;
                    break;
                }
            }

            strncpy_s(config->HideClans.m_ManualNames[count], ally.name.c_str(), _TRUNCATE);
            config->HideClans.m_ManualShowList[count] = existingIndex >= 0 ? config->HideClans.m_ManualShowList[existingIndex] : false;
            ++count;
        }
        config->HideClans.m_ManualCount = count;

        for (auto& ally : allies) {
            if (!ally.player) {
                continue;
            }

            bool shouldShow = false;
            for (int index = 0; index < count; ++index) {
                if (ally.name == config->HideClans.m_ManualNames[index]) {
                    shouldShow = config->HideClans.m_ManualShowList[index];
                    break;
                }
            }

            try {
                if (shouldShow) {
                    ally.player->Restore(env);
                } else {
                    ally.player->Zero(env);
                    affectedPlayers = true;
                }
            } catch (...) {}
            env->DeleteLocalRef(reinterpret_cast<jobject>(ally.player));
        }
    }

    if (affectedPlayers) {
        MarkInUse(200);
    } else {
        ClearInUse();
    }

    if (localTeam) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(localTeam));
    }
    if (scoreboard) {
        env->DeleteLocalRef(reinterpret_cast<jobject>(scoreboard));
    }
    env->DeleteLocalRef(localPlayerObject);
    env->DeleteLocalRef(worldObject);
}
