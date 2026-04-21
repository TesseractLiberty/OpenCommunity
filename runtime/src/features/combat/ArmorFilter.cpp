#include "pch.h"
#include "ArmorFilter.h"

namespace {
    void ReleaseLocal(JNIEnv* env, jobject object) {
        if (env && object) {
            env->DeleteLocalRef(object);
        }
    }
}

void ArmorFilter::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (env) Run(env);
}

void ArmorFilter::Run(JNIEnv* env) {
    if (!IsEnabled() || !env) {
        return;
    }

    jobject minecraft = Minecraft::GetTheMinecraft(env);
    if (!minecraft) {
        return;
    }

    jobject playerObject = Minecraft::GetThePlayer(env);
    if (!playerObject) {
        ReleaseLocal(env, minecraft);
        return;
    }

    jobject screenObject = Minecraft::GetCurrentScreen(env);
    if (!screenObject) {
        ReleaseLocal(env, playerObject);
        ReleaseLocal(env, minecraft);
        return;
    }

    auto* screen = reinterpret_cast<GuiScreen*>(screenObject);
    if (!screen->IsInventory(env)) {
        ReleaseLocal(env, screenObject);
        ReleaseLocal(env, playerObject);
        ReleaseLocal(env, minecraft);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto delay = std::chrono::milliseconds((std::max)(0, GetDelay()));
    if (now - m_LastActionTime < delay) {
        ReleaseLocal(env, screenObject);
        ReleaseLocal(env, playerObject);
        ReleaseLocal(env, minecraft);
        return;
    }

    auto* player = reinterpret_cast<Player*>(playerObject);
    jobject containerObject = player->GetInventoryContainer(env);
    if (!containerObject) {
        ReleaseLocal(env, screenObject);
        ReleaseLocal(env, playerObject);
        ReleaseLocal(env, minecraft);
        return;
    }

    auto* container = reinterpret_cast<Container*>(containerObject);
    const int windowId = container->GetWindowId(env);
    const int durabilityThreshold = (std::clamp)(GetDurabilityThreshold(), 0, 100);

    for (int slotId = 9; slotId <= 44; ++slotId) {
        jobject slotObject = container->GetSlot(slotId, env);
        if (!slotObject) {
            continue;
        }

        auto* slot = reinterpret_cast<Slot*>(slotObject);
        jobject stackObject = slot->GetStack(env);
        if (!stackObject) {
            ReleaseLocal(env, slotObject);
            continue;
        }

        auto* stack = reinterpret_cast<ItemStack*>(stackObject);
        if (stack->IsArmor(env)) {
            const int maxDamage = stack->GetMaxDamage(env);
            if (maxDamage > 0) {
                const int damage = stack->GetItemDamage(env);
                const int remaining = maxDamage - damage;
                const int percentage = (remaining * 100) / maxDamage;

                if (percentage <= durabilityThreshold) {
                    PlayerController::WindowClick(playerObject, windowId, slotId, 1, 4, env);
                    m_LastActionTime = std::chrono::steady_clock::now();
                    MarkInUse(250);
                    ReleaseLocal(env, stackObject);
                    ReleaseLocal(env, slotObject);
                    break;
                }
            }
        }

        ReleaseLocal(env, stackObject);
        ReleaseLocal(env, slotObject);
    }

    ReleaseLocal(env, containerObject);
    ReleaseLocal(env, screenObject);
    ReleaseLocal(env, playerObject);
    ReleaseLocal(env, minecraft);
}
