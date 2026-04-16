#include "pch.h"
#include "ArmorSwap.h"

#include <algorithm>
#include <cmath>

bool ArmorSwap::s_IsWorking = false;

void ArmorSwap::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (env) {
        Run(env);
    }
}

void ArmorSwap::Run(JNIEnv* env) {
    if (!env) {
        return;
    }

    if (env->PushLocalFrame(100) != 0) {
        return;
    }

    if (!IsEnabled()) {
        if (m_State != State::Idle) {
            ResetWorkState(env);
        }
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject playerObject = Minecraft::GetThePlayer(env);
    if (!playerObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject worldObject = Minecraft::GetTheWorld(env);
    if (!worldObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* player = reinterpret_cast<Player*>(playerObject);
    const auto now = std::chrono::steady_clock::now();
    const auto actionDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastActionTime).count();
    const int minDelay = (std::max)(1, GetDelay());

    if (actionDiff < minDelay) {
        env->PopLocalFrame(nullptr);
        return;
    }

    s_IsWorking = (m_State != State::Idle);

    if (m_State == State::Idle) {
        const auto lastCloseDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastCloseTime).count();
        if (lastCloseDiff < 500) {
            env->PopLocalFrame(nullptr);
            return;
        }

        jobject containerObject = player->GetInventoryContainer(env);
        if (!containerObject) {
            env->PopLocalFrame(nullptr);
            return;
        }

        auto* container = reinterpret_cast<Container*>(containerObject);
        bool canSwapSomething = false;
        bool missingAnyPiece = false;
        bool hasAnyArmorInInventory = false;
        bool hasNewArmorInInventory = false;

        struct ArmorInfo {
            jobject equipped = nullptr;
            int threshold = 0;
            long bestValue = -1;
            int bestSlot = -1;
        } armors[4];

        for (int type = 0; type < 4; ++type) {
            armors[type].equipped = GetEquippedByType(type, playerObject, env);
            if (!armors[type].equipped) {
                missingAnyPiece = true;
            }

            if (IsSwapAllEnabled()) {
                armors[type].threshold = GetPercentage();
            } else {
                switch (type) {
                case 0: armors[type].threshold = GetHelmetPct(); break;
                case 1: armors[type].threshold = GetChestPct(); break;
                case 2: armors[type].threshold = GetLegsPct(); break;
                case 3: armors[type].threshold = GetBootsPct(); break;
                }
            }

            armors[type].bestValue = GetArmorValue(armors[type].equipped, env);
        }

        const bool useInventoryOrganizer = IsInventoryOrganizerEnabled();
        for (int slotId = 9; slotId <= 44; ++slotId) {
            jobject slotObject = container->GetSlot(slotId, env);
            if (!slotObject) {
                continue;
            }

            auto* slot = reinterpret_cast<Slot*>(slotObject);
            jobject stackObject = slot->GetStack(env);
            if (!stackObject) {
                env->DeleteLocalRef(slotObject);
                continue;
            }

            auto* stack = reinterpret_cast<ItemStack*>(stackObject);
            if (stack->IsArmor(env)) {
                jobject itemObject = stack->GetItem(env);
                if (itemObject) {
                    const int type = ItemArmor::GetArmorType(itemObject, env);
                    if (type >= 0 && type < 4) {
                        hasAnyArmorInInventory = true;
                        const int durability = GetDurabilityPct(stackObject, env);
                        if (durability > armors[type].threshold) {
                            hasNewArmorInInventory = true;
                        }

                        const long value = (long)ItemArmor::GetDamageReduceAmount(itemObject, env) * 1000 + durability;
                        bool isBetter = false;

                        if (value > armors[type].bestValue) {
                            isBetter = true;
                        } else if (useInventoryOrganizer && slotId >= 36 && value == armors[type].bestValue && armors[type].bestSlot < 36) {
                            isBetter = true;
                        }

                        if (isBetter) {
                            armors[type].bestValue = value;
                            armors[type].bestSlot = slotId;
                        }
                    }
                    env->DeleteLocalRef(itemObject);
                }
            }

            env->DeleteLocalRef(stackObject);
            env->DeleteLocalRef(slotObject);
        }

        bool needsArmor = false;
        if (IsSwapAllEnabled()) {
            bool anyPieceBad = false;
            for (int type = 0; type < 4; ++type) {
                const int currentDurability = GetDurabilityPct(armors[type].equipped, env);
                if (!armors[type].equipped || currentDurability <= armors[type].threshold) {
                    anyPieceBad = true;
                    break;
                }
            }

            if (anyPieceBad) {
                for (int type = 0; type < 4; ++type) {
                    m_ToReplace[type] = false;
                    if (armors[type].bestSlot != -1) {
                        m_ToReplace[type] = true;
                        canSwapSomething = true;
                    }
                }
                if (!canSwapSomething) {
                    needsArmor = true;
                }
            }
        } else {
            for (int type = 0; type < 4; ++type) {
                m_ToReplace[type] = false;
                const int currentDurability = GetDurabilityPct(armors[type].equipped, env);
                if (armors[type].bestSlot != -1 && (!armors[type].equipped || currentDurability <= armors[type].threshold)) {
                    m_ToReplace[type] = true;
                    canSwapSomething = true;
                }
                if (!armors[type].equipped || currentDurability <= armors[type].threshold) {
                    needsArmor = true;
                }
            }
        }

        if (canSwapSomething) {
            m_State = State::Opening;
            s_IsWorking = true;
            m_StatusMsg = "Trocando set";
            m_StatusTime = now;
        } else if (needsArmor) {
            if (missingAnyPiece && !hasAnyArmorInInventory) {
                m_StatusMsg = "Sem set";
                m_StatusTime = now;
            } else if (hasAnyArmorInInventory && !hasNewArmorInInventory) {
                m_StatusMsg = "Sem set + novo";
                m_StatusTime = now;
            }
        }

        for (auto& armor : armors) {
            if (armor.equipped) {
                env->DeleteLocalRef(armor.equipped);
            }
        }

        env->DeleteLocalRef(containerObject);
        m_LastActionTime = now;
        env->PopLocalFrame(nullptr);
        return;
    }

    switch (m_State) {
    case State::Opening: {
        jobject screenObject = Minecraft::GetCurrentScreen(env);
        auto* gui = reinterpret_cast<GuiScreen*>(screenObject);

        if (!screenObject || !gui->IsInventory(env)) {
            jobject packet = PacketClientStatus::Create(env, 2);
            if (packet) {
                player->SendPacket(packet, env);
                env->DeleteLocalRef(packet);
            }

            jobject inventoryScreen = Minecraft::CreateGuiInventory(playerObject, env);
            if (inventoryScreen) {
                Minecraft::DisplayGuiScreen(inventoryScreen, env);
                env->DeleteLocalRef(inventoryScreen);
            }

            m_LastActionTime = now;
        } else {
            m_State = State::Processing;
        }

        if (screenObject) {
            env->DeleteLocalRef(screenObject);
        }
        break;
    }

    case State::Processing: {
        jobject screenObject = Minecraft::GetCurrentScreen(env);
        auto* gui = reinterpret_cast<GuiScreen*>(screenObject);
        if (!screenObject || !gui->IsInventory(env)) {
            m_State = State::Idle;
            m_FinishTime = now;
            if (screenObject) {
                env->DeleteLocalRef(screenObject);
            }
            env->PopLocalFrame(nullptr);
            return;
        }

        bool swappedAny = false;
        for (int index = 0; index < 4; ++index) {
            if (!m_ToReplace[index]) {
                continue;
            }

            jobject current = GetEquippedByType(index, playerObject, env);
            const int replacementSlot = FindBestReplacementSlot(index, current, playerObject, env);
            if (current) {
                env->DeleteLocalRef(current);
            }

            if (replacementSlot != -1) {
                const int armorSlot = 5 + index;
                jobject containerObject = player->GetInventoryContainer(env);
                if (containerObject) {
                    auto* container = reinterpret_cast<Container*>(containerObject);
                    const int windowId = container->GetWindowId(env);

                    PlayerController::WindowClick(playerObject, windowId, replacementSlot, 0, 0, env);
                    PlayerController::WindowClick(playerObject, windowId, armorSlot, 0, 0, env);
                    PlayerController::WindowClick(playerObject, windowId, replacementSlot, 0, 0, env);

                    if (IsAutoDropEnabled()) {
                        PlayerController::WindowClick(playerObject, windowId, replacementSlot, 1, 4, env);
                    }

                    swappedAny = true;
                    m_ToReplace[index] = false;
                    env->DeleteLocalRef(containerObject);
                }

                if (!IsMultiSwapEnabled()) {
                    break;
                }
            } else {
                m_ToReplace[index] = false;
            }
        }

        if (swappedAny) {
            m_LastActionTime = now;
        }

        bool stillToReplace = false;
        for (bool value : m_ToReplace) {
            if (value) {
                stillToReplace = true;
                break;
            }
        }

        if (!stillToReplace) {
            m_State = IsInventoryOrganizerEnabled() ? State::Organizing : State::Closing;
        }

        if (screenObject) {
            env->DeleteLocalRef(screenObject);
        }
        break;
    }

    case State::Organizing:
        OrganizeInventory(playerObject, env);
        m_LastActionTime = now;
        m_State = State::Closing;
        break;

    case State::Closing: {
        jobject screenObject = Minecraft::GetCurrentScreen(env);
        if (screenObject) {
            auto* gui = reinterpret_cast<GuiScreen*>(screenObject);
            if (gui->IsInventory(env)) {
                Minecraft::DisplayGuiScreen(nullptr, env);
            }
            env->DeleteLocalRef(screenObject);
        }

        m_LastCloseTime = now;
        m_FinishTime = now;
        m_StatusMsg = "Set trocado";
        m_StatusTime = now;
        ResetWorkState(env);
        m_LastActionTime = now;
        break;
    }

    case State::Idle:
        break;
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    env->PopLocalFrame(nullptr);
}

void ArmorSwap::OrganizeInventory(jobject playerObject, JNIEnv* env) {
    if (!env || !playerObject) {
        return;
    }

    auto* player = reinterpret_cast<Player*>(playerObject);
    jobject containerObject = player->GetInventoryContainer(env);
    if (!containerObject) {
        return;
    }

    auto* container = reinterpret_cast<Container*>(containerObject);
    const int windowId = container->GetWindowId(env);
    const int slotMap[4] = { GetHelmetSlot(), GetChestSlot(), GetLegsSlot(), GetBootsSlot() };

    for (int type = 0; type < 4; ++type) {
        const int targetSlot = 35 + slotMap[type];
        if (targetSlot < 36 || targetSlot > 44) {
            continue;
        }

        for (int slotId = 9; slotId <= 35; ++slotId) {
            jobject slotObject = container->GetSlot(slotId, env);
            if (!slotObject) {
                continue;
            }

            auto* slot = reinterpret_cast<Slot*>(slotObject);
            jobject stackObject = slot->GetStack(env);
            if (!stackObject) {
                env->DeleteLocalRef(slotObject);
                continue;
            }

            auto* stack = reinterpret_cast<ItemStack*>(stackObject);
            if (stack->IsArmor(env)) {
                jobject itemObject = stack->GetItem(env);
                if (itemObject) {
                    if (ItemArmor::GetArmorType(itemObject, env) == type) {
                        PlayerController::WindowClick(playerObject, windowId, slotId, 0, 0, env);
                        PlayerController::WindowClick(playerObject, windowId, targetSlot, 0, 0, env);
                        PlayerController::WindowClick(playerObject, windowId, slotId, 0, 0, env);
                        env->DeleteLocalRef(itemObject);
                        env->DeleteLocalRef(stackObject);
                        env->DeleteLocalRef(slotObject);
                        break;
                    }
                    env->DeleteLocalRef(itemObject);
                }
            }

            env->DeleteLocalRef(stackObject);
            env->DeleteLocalRef(slotObject);
        }
    }

    env->DeleteLocalRef(containerObject);
}

int ArmorSwap::FindBestInRange(int armorType, jobject current, jobject playerObject, int startSlot, int endSlot, JNIEnv* env) {
    if (!env || !playerObject) {
        return -1;
    }

    auto* player = reinterpret_cast<Player*>(playerObject);
    jobject containerObject = player->GetInventoryContainer(env);
    if (!containerObject) {
        return -1;
    }

    auto* container = reinterpret_cast<Container*>(containerObject);
    int bestSlot = -1;
    long bestValue = GetArmorValue(current, env);

    for (int slotId = startSlot; slotId <= endSlot; ++slotId) {
        jobject slotObject = container->GetSlot(slotId, env);
        if (!slotObject) {
            continue;
        }

        auto* slot = reinterpret_cast<Slot*>(slotObject);
        jobject stackObject = slot->GetStack(env);
        if (!stackObject) {
            env->DeleteLocalRef(slotObject);
            continue;
        }

        auto* stack = reinterpret_cast<ItemStack*>(stackObject);
        if (stack->IsArmor(env)) {
            jobject itemObject = stack->GetItem(env);
            if (itemObject) {
                if (ItemArmor::GetArmorType(itemObject, env) == armorType) {
                    const long value = GetArmorValue(stackObject, env);
                    if (value > bestValue) {
                        bestValue = value;
                        bestSlot = slotId;
                    }
                }
                env->DeleteLocalRef(itemObject);
            }
        }

        env->DeleteLocalRef(stackObject);
        env->DeleteLocalRef(slotObject);
    }

    env->DeleteLocalRef(containerObject);
    return bestSlot;
}

void ArmorSwap::ResetWorkState(JNIEnv* env) {
    (void)env;
    m_State = State::Idle;
    s_IsWorking = false;
    for (bool& value : m_ToReplace) {
        value = false;
    }
    m_PendingDrops.clear();
}

int ArmorSwap::GetDurabilityPct(jobject stackObject, JNIEnv* env) {
    if (!stackObject || !env) {
        return 0;
    }

    auto* stack = reinterpret_cast<ItemStack*>(stackObject);
    if (!stack->IsArmor(env)) {
        return 0;
    }

    const int maxDamage = stack->GetMaxDamage(env);
    if (maxDamage <= 0) {
        return 100;
    }

    const int damage = stack->GetItemDamage(env);
    int remaining = maxDamage - damage;
    if (remaining < 0) {
        remaining = 0;
    }
    if (remaining > maxDamage) {
        remaining = maxDamage;
    }

    return (int)std::floor((remaining * 100.0) / (double)maxDamage);
}

long ArmorSwap::GetArmorValue(jobject stackObject, JNIEnv* env) {
    if (!stackObject || !env) {
        return -1;
    }

    auto* stack = reinterpret_cast<ItemStack*>(stackObject);
    if (!stack->IsArmor(env)) {
        return -1;
    }

    jobject itemObject = stack->GetItem(env);
    if (!itemObject) {
        return -1;
    }

    const int armorValue = ItemArmor::GetDamageReduceAmount(itemObject, env);
    env->DeleteLocalRef(itemObject);
    return (long)armorValue * 1000 + GetDurabilityPct(stackObject, env);
}

jobject ArmorSwap::GetEquippedByType(int armorType, jobject playerObject, JNIEnv* env) {
    auto* player = reinterpret_cast<Player*>(playerObject);
    return player ? player->GetCurrentArmor(3 - armorType, env) : nullptr;
}

int ArmorSwap::FindBestReplacementSlot(int armorType, jobject current, jobject playerObject, JNIEnv* env) {
    if (IsInventoryOrganizerEnabled()) {
        const int hotbarResult = FindBestInRange(armorType, current, playerObject, 36, 44, env);
        if (hotbarResult != -1) {
            return hotbarResult;
        }
        return FindBestInRange(armorType, current, playerObject, 9, 35, env);
    }

    return FindBestInRange(armorType, current, playerObject, 9, 44, env);
}
