#include "pch.h"
#include "AutoGapple.h"

#include <chrono>

namespace {
    long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    bool g_consuming = false;
    int g_previousHotbarSlot = -1;
    long long g_startedConsumeAtMs = 0;
    int g_lockedAppleSlot = -1;
    long long g_lastAttemptAtMs = 0;

    void RestorePreviousSlot(JNIEnv* env, Player* player) {
        if (!env || !player || g_previousHotbarSlot < 0 || g_previousHotbarSlot > 8) {
            return;
        }

        jobject inventoryObject = player->GetInventoryPlayer(env);
        if (!inventoryObject) {
            return;
        }

        auto* inventory = reinterpret_cast<InventoryPlayer*>(inventoryObject);
        if (inventory->GetCurrentItem(env) != g_previousHotbarSlot) {
            inventory->SetCurrentItem(g_previousHotbarSlot, env);
            PlayerController::UpdateController(env);
        }

        env->DeleteLocalRef(inventoryObject);
    }

    int FindGoldenAppleInHotbar(JNIEnv* env, Player* player) {
        if (!env || !player) {
            return -1;
        }

        jobject inventoryObject = player->GetInventoryPlayer(env);
        if (!inventoryObject) {
            return -1;
        }

        auto* inventory = reinterpret_cast<InventoryPlayer*>(inventoryObject);
        int appleSlot = -1;

        for (int slot = 0; slot <= 8; ++slot) {
            jobject stackObject = inventory->GetStackInSlot(slot, env);
            if (!stackObject) {
                continue;
            }

            auto* stack = reinterpret_cast<ItemStack*>(stackObject);
            if (stack->IsAppleGold(env)) {
                appleSlot = slot;
                if (stack->GetMetadata(env) == 1) {
                    env->DeleteLocalRef(stackObject);
                    break;
                }
            }

            env->DeleteLocalRef(stackObject);
        }

        env->DeleteLocalRef(inventoryObject);
        return appleSlot;
    }

    void EnforceLockedSlotDuringConsume(JNIEnv* env, Player* player) {
        if (!env || !player || g_lockedAppleSlot < 0 || g_lockedAppleSlot > 8) {
            return;
        }

        jobject inventoryObject = player->GetInventoryPlayer(env);
        if (!inventoryObject) {
            return;
        }

        auto* inventory = reinterpret_cast<InventoryPlayer*>(inventoryObject);
        if (inventory->GetCurrentItem(env) != g_lockedAppleSlot) {
            inventory->SetCurrentItem(g_lockedAppleSlot, env);
            PlayerController::UpdateController(env);
        }

        env->DeleteLocalRef(inventoryObject);
    }
}

void AutoGapple::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (env) {
        Run(env);
    }
}

void AutoGapple::Run(JNIEnv* env) {
    if (!env) {
        return;
    }

    if (env->PushLocalFrame(50) != 0) {
        return;
    }

    jobject playerObject = Minecraft::GetThePlayer(env);
    if (!playerObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* player = reinterpret_cast<Player*>(playerObject);

    if (!IsEnabled()) {
        if (g_consuming) {
            jobject useItemKey = Minecraft::GetKeyBindUseItem(env);
            if (useItemKey) {
                const int keyCode = reinterpret_cast<KeyBinding*>(useItemKey)->GetKeyCode(env);
                KeyBinding::SetKeyBindState(keyCode, false, env);
                env->DeleteLocalRef(useItemKey);
            }

            g_lockedAppleSlot = -1;
            g_consuming = false;
            RestorePreviousSlot(env, player);
            g_previousHotbarSlot = -1;
        }

        env->PopLocalFrame(nullptr);
        return;
    }

    const long long nowMs = NowMs();

    if (g_consuming) {
        MarkInUse(200);
        EnforceLockedSlotDuringConsume(env, player);

        jobject useItemKey = Minecraft::GetKeyBindUseItem(env);
        const bool stillUsing = player->IsUsingItem(env);
        const bool timedOut = (nowMs - g_startedConsumeAtMs) > 3000;

        if (stillUsing && !timedOut) {
            if (useItemKey) {
                const int keyCode = reinterpret_cast<KeyBinding*>(useItemKey)->GetKeyCode(env);
                KeyBinding::SetKeyBindState(keyCode, true, env);
                env->DeleteLocalRef(useItemKey);
            }

            env->PopLocalFrame(nullptr);
            return;
        }

        if (useItemKey) {
            const int keyCode = reinterpret_cast<KeyBinding*>(useItemKey)->GetKeyCode(env);
            KeyBinding::SetKeyBindState(keyCode, false, env);
            env->DeleteLocalRef(useItemKey);
        }

        RestorePreviousSlot(env, player);
        g_consuming = false;
        g_previousHotbarSlot = -1;
        g_lockedAppleSlot = -1;
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject currentScreenObject = Minecraft::GetCurrentScreen(env);
    if (currentScreenObject) {
        env->DeleteLocalRef(currentScreenObject);
        env->PopLocalFrame(nullptr);
        return;
    }

    bool shouldEat = true;
    jobject potionEffectObject = player->GetActivePotionEffect(10, env);
    if (potionEffectObject) {
        const int durationTicks = reinterpret_cast<PotionEffect*>(potionEffectObject)->GetDuration(env);
        shouldEat = durationTicks <= GetDelaySeconds() * 20;
        env->DeleteLocalRef(potionEffectObject);
    }

    if (!shouldEat || (nowMs - g_lastAttemptAtMs) < 300) {
        env->PopLocalFrame(nullptr);
        return;
    }

    const int appleSlot = FindGoldenAppleInHotbar(env, player);
    if (appleSlot == -1) {
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject inventoryObject = player->GetInventoryPlayer(env);
    if (!inventoryObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* inventory = reinterpret_cast<InventoryPlayer*>(inventoryObject);
    g_previousHotbarSlot = inventory->GetCurrentItem(env);
    if (g_previousHotbarSlot != appleSlot) {
        inventory->SetCurrentItem(appleSlot, env);
        PlayerController::UpdateController(env);
    }
    env->DeleteLocalRef(inventoryObject);

    g_lockedAppleSlot = appleSlot;

    jobject useItemKey = Minecraft::GetKeyBindUseItem(env);
    if (useItemKey) {
        const int keyCode = reinterpret_cast<KeyBinding*>(useItemKey)->GetKeyCode(env);
        KeyBinding::SetKeyBindState(keyCode, true, env);
        env->DeleteLocalRef(useItemKey);
    }

    jobject heldItemObject = player->GetHeldItem(env);
    jobject worldObject = Minecraft::GetTheWorld(env);
    if (heldItemObject && worldObject) {
        PlayerController::SendUseItem(playerObject, worldObject, heldItemObject, env);
    }

    g_consuming = true;
    g_startedConsumeAtMs = nowMs;
    g_lastAttemptAtMs = nowMs;
    MarkInUse(300);
    env->PopLocalFrame(nullptr);
}
