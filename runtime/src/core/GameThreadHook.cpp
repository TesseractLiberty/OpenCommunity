#include "pch.h"
#include "GameThreadHook.h"
#include "../../../deps/minhook/MinHook.h"
#include "../../../shared/common/modules/ModuleManager.h"
#include "../game/classes/Minecraft.h"
#include "../game/classes/MovingObjectPosition.h"
#include "../game/classes/Player.h"
#include "../game/jni/Field.h"
#include "../game/jni/GameInstance.h"
#include "../game/jni/JniRefs.h"
#include "../game/mapping/Mapper.h"
#include "../features/visuals/EnemyInfoList.h"
#include "../features/visuals/Target.h"
#include <jni.h>
#include <atomic>
#include <chrono>

typedef double(JNICALL* tStrictMathAtan2)(JNIEnv* env, jclass klass, double x, double y);
static tStrictMathAtan2 g_OrigAtan2 = nullptr;
static void* g_HookedAddr = nullptr;
static std::atomic<bool> g_Installed = false;
static std::atomic<long long> g_LastHookTickMs = 0;
static std::atomic<long long> g_LastFallbackTickMs = 0;
static jvmtiEnv* g_Jvmti = nullptr;
static jmethodID g_ClickMouseMethod = nullptr;
static jlocation g_ClickMouseBreakpointLocation = 0;
static bool g_BreakpointCallbacksInstalled = false;
static bool g_ClickMouseBreakpointInstalled = false;

namespace {
    long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool IsHiddenPlayerMouseOver(JNIEnv* env) {
        if (!env || !g_Game || !g_Game->IsInitialized()) {
            return false;
        }

        JniLocalRef<jobject> mouseOverObject(env, Minecraft::GetObjectMouseOver(env));
        if (!mouseOverObject) {
            return false;
        }

        bool shouldClear = false;
        auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject.Get());
        if (mouseOver->IsAimingEntity(env)) {
            JniLocalRef<jobject> entityObject(env, mouseOver->GetEntity(env));
            if (entityObject) {
                Class* playerClass = g_Game->FindClass(Mapper::Get("net/minecraft/entity/player/EntityPlayer"));
                if (playerClass && env->IsInstanceOf(entityObject.Get(), reinterpret_cast<jclass>(playerClass))) {
                    shouldClear = reinterpret_cast<Player*>(entityObject.Get())->HasZeroedBoundingBox(env);
                }
            }
        }

        return shouldClear;
    }

    void SanitizeHiddenObjectMouseOver(JNIEnv* env) {
        if (!env) {
            return;
        }

        if (IsHiddenPlayerMouseOver(env)) {
            Minecraft::SetObjectMouseOver(nullptr, env);
        }
    }

    bool IsLeftClickReady(JNIEnv* env) {
        if (!env || !g_Game || !g_Game->IsInitialized()) {
            return false;
        }

        Class* minecraftClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/Minecraft"));
        const std::string fieldName = Mapper::Get("leftClickCounter");
        if (!minecraftClass || fieldName.empty()) {
            return true;
        }

        Field* field = minecraftClass->GetField(env, fieldName.c_str(), "I");
        if (!field) {
            return true;
        }

        JniLocalRef<jobject> minecraft(env, Minecraft::GetTheMinecraft(env));
        if (!minecraft) {
            return true;
        }

        const int leftClickCounter = field->GetIntField(env, minecraft.Get());
        return leftClickCounter <= 0;
    }

    void NotifyTargetLocalAttack(JNIEnv* env) {
        if (!env || !g_Game || !g_Game->IsInitialized()) {
            return;
        }

        if (!IsLeftClickReady(env)) {
            return;
        }

        JniLocalRef<jobject> mouseOverObject(env, Minecraft::GetObjectMouseOver(env));
        if (!mouseOverObject) {
            return;
        }

        auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject.Get());
        if (mouseOver->IsAimingEntity(env)) {
            JniLocalRef<jobject> entityObject(env, mouseOver->GetEntity(env));
            if (entityObject) {
                Class* playerClass = g_Game->FindClass(Mapper::Get("net/minecraft/entity/player/EntityPlayer"));
                if (playerClass && env->IsInstanceOf(entityObject.Get(), reinterpret_cast<jclass>(playerClass))) {
                    JniLocalRef<jobject> localPlayerObject(env, Minecraft::GetThePlayer(env));
                        if (!localPlayerObject || !env->IsSameObject(entityObject.Get(), localPlayerObject.Get())) {
                            Target::OnLocalAttack(env, reinterpret_cast<Player*>(entityObject.Get()));
                            EnemyInfoList::OnLocalAttack(env, reinterpret_cast<Player*>(entityObject.Get()));
                        }
                }
            }
        }
    }

    bool SetBreakpointAtMethodStart(jmethodID method, jlocation& breakpointLocation) {
        if (!g_Jvmti || !method) {
            return false;
        }

        jlocation startLocation = 0;
        jlocation endLocation = 0;
        if (g_Jvmti->GetMethodLocation(method, &startLocation, &endLocation) != JVMTI_ERROR_NONE) {
            startLocation = 0;
        }

        if (g_Jvmti->SetBreakpoint(method, startLocation) != JVMTI_ERROR_NONE) {
            return false;
        }

        breakpointLocation = startLocation;
        return true;
    }

    void JNICALL BreakpointCallback(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jmethodID method, jlocation location) {
        (void)location;
        (void)jvmti;
        (void)thread;

        if (!env) {
            return;
        }

        if (method == g_ClickMouseMethod) {
            JniLocalFrame frame(env, 512);
            if (frame.IsActive()) {
                SanitizeHiddenObjectMouseOver(env);
                NotifyTargetLocalAttack(env);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
            }
            return;
        }
    }

    bool EnsureBreakpointCallbacks() {
        if (!g_Jvmti) {
            return false;
        }

        if (!g_BreakpointCallbacksInstalled) {
            jvmtiEventCallbacks callbacks{};
            callbacks.Breakpoint = &BreakpointCallback;

            if (g_Jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
                return false;
            }

            g_BreakpointCallbacksInstalled = true;
        }

        return g_Jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr) == JVMTI_ERROR_NONE;
    }

    bool InitializeClickMouseBreakpoint() {
        if (!g_Game || !g_Game->IsInitialized()) {
            return false;
        }

        JNIEnv* env = g_Game->GetCurrentEnv();
        g_Jvmti = g_Game->GetJVMTI();
        if (!env || !g_Jvmti) {
            return false;
        }

        if (!EnsureBreakpointCallbacks()) {
            return false;
        }

        const std::string minecraftClassName = Mapper::Get("net/minecraft/client/Minecraft");
        const std::string clickMouseName = Mapper::Get("clickMouse");
        if (minecraftClassName.empty() || clickMouseName.empty()) {
            return false;
        }

        Class* minecraftClass = g_Game->FindClass(minecraftClassName);
        if (!minecraftClass) {
            return false;
        }

        g_ClickMouseMethod = env->GetMethodID(reinterpret_cast<jclass>(minecraftClass), clickMouseName.c_str(), "()V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        if (!g_ClickMouseMethod) {
            return false;
        }

        if (!SetBreakpointAtMethodStart(g_ClickMouseMethod, g_ClickMouseBreakpointLocation)) {
            g_ClickMouseMethod = nullptr;
            return false;
        }

        g_ClickMouseBreakpointInstalled = true;
        return true;
    }

    void ShutdownBreakpoints() {
        if (!g_Jvmti) {
            g_ClickMouseMethod = nullptr;
            g_BreakpointCallbacksInstalled = false;
            g_ClickMouseBreakpointInstalled = false;
            return;
        }

        if (g_ClickMouseBreakpointInstalled && g_ClickMouseMethod) {
            g_Jvmti->ClearBreakpoint(g_ClickMouseMethod, g_ClickMouseBreakpointLocation);
        }

        g_Jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);

        jvmtiEventCallbacks callbacks{};
        g_Jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        g_ClickMouseMethod = nullptr;
        g_BreakpointCallbacksInstalled = false;
        g_ClickMouseBreakpointInstalled = false;
        g_Jvmti = nullptr;
    }
}

double JNICALL StrictMathAtan2Hook(JNIEnv* env, jclass klass, double x, double y)
{
    if (env && klass) {
        g_LastHookTickMs.store(NowMs(), std::memory_order_relaxed);
        JniLocalFrame frame(env, 512);
        if (frame.IsActive()) {
            SanitizeHiddenObjectMouseOver(env);
            ModuleManager::Get()->TickSynchronousAll(env);
            SanitizeHiddenObjectMouseOver(env);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
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
    InitializeClickMouseBreakpoint();
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

void GameThreadHook::SanitizeInteractionState(void* envPtr)
{
    auto* env = static_cast<JNIEnv*>(envPtr);
    if (!env) {
        return;
    }

    SanitizeHiddenObjectMouseOver(env);
}

void GameThreadHook::Shutdown()
{
    ShutdownBreakpoints();

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
