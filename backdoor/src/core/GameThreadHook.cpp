#include "pch.h"
#include "GameThreadHook.h"
#include "../../../deps/minhook/MinHook.h"
#include "../../../shared/common/FeatureManager.h"
#include "../game/jni/GameInstance.h"
#include <jni.h>
#include <atomic>
#include <chrono>

typedef double(JNICALL* tStrictMathAtan2)(JNIEnv* env, jclass klass, double x, double y);
static tStrictMathAtan2 g_OrigAtan2 = nullptr;
static void* g_HookedAddr = nullptr;
static std::atomic<bool> g_Installed = false;
static std::atomic<long long> g_LastHookTickMs = 0;
static std::atomic<long long> g_LastFallbackTickMs = 0;

namespace {
    long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

double JNICALL StrictMathAtan2Hook(JNIEnv* env, jclass klass, double x, double y)
{
    if (env && klass) {
        g_LastHookTickMs.store(NowMs(), std::memory_order_relaxed);
        if (env->PushLocalFrame(512) == 0) {
            FeatureManager::Get()->TickSynchronousAll(env);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->PopLocalFrame(nullptr);
        }
    }
    return g_OrigAtan2(env, klass, x, y);
}

bool GameThreadHook::Initialize()
{
    g_Installed.store(false, std::memory_order_relaxed);
    g_LastHookTickMs.store(0, std::memory_order_relaxed);
    g_LastFallbackTickMs.store(0, std::memory_order_relaxed);

    HMODULE hJava = GetModuleHandleW(L"java.dll");
    if (!hJava) hJava = GetModuleHandleW(L"jvm.dll");
    if (!hJava) return false;

    void* pAtan2 = (void*)GetProcAddress(hJava, "Java_java_lang_StrictMath_atan2");
    if (!pAtan2) {
        hJava = GetModuleHandleW(L"jvm.dll");
        if (hJava) pAtan2 = (void*)GetProcAddress(hJava, "Java_java_lang_StrictMath_atan2");
    }
    if (!pAtan2) return false;

    if (MH_CreateHook(pAtan2, (void*)StrictMathAtan2Hook, (void**)&g_OrigAtan2) != MH_OK) {
        return false;
    }

    if (MH_EnableHook(pAtan2) != MH_OK) {
        return false;
    }

    g_HookedAddr = pAtan2;
    g_Installed.store(true, std::memory_order_relaxed);
    return true;
}

bool GameThreadHook::ShouldRunFallback()
{
    const long long nowMs = NowMs();
    const long long lastHookTickMs = g_LastHookTickMs.load(std::memory_order_relaxed);
    const long long lastFallbackTickMs = g_LastFallbackTickMs.load(std::memory_order_relaxed);

    if (g_Installed.load(std::memory_order_relaxed) && lastHookTickMs != 0 && (nowMs - lastHookTickMs) <= 250) {
        return false;
    }

    if ((nowMs - lastFallbackTickMs) < 10) {
        return false;
    }

    g_LastFallbackTickMs.store(nowMs, std::memory_order_relaxed);
    return true;
}

void GameThreadHook::Shutdown()
{
    if (g_HookedAddr) {
        MH_DisableHook(g_HookedAddr);
        MH_RemoveHook(g_HookedAddr);
        g_HookedAddr = nullptr;
        g_OrigAtan2 = nullptr;
    }

    g_Installed.store(false, std::memory_order_relaxed);
    g_LastHookTickMs.store(0, std::memory_order_relaxed);
    g_LastFallbackTickMs.store(0, std::memory_order_relaxed);
}
