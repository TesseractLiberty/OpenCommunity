#include "pch.h"
#include "GameThreadHook.h"
#include "../../../deps/minhook/MinHook.h"
#include "../../../shared/common/FeatureManager.h"
#include "../game/jni/GameInstance.h"
#include <jni.h>

typedef double(JNICALL* tStrictMathAtan2)(JNIEnv* env, jclass klass, double x, double y);
static tStrictMathAtan2 g_OrigAtan2 = nullptr;
static void* g_HookedAddr = nullptr;

double JNICALL StrictMathAtan2Hook(JNIEnv* env, jclass klass, double x, double y)
{
    if (env && klass) {
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
}
