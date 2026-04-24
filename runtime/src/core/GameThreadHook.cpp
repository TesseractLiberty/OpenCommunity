#include "pch.h"
#include "GameThreadHook.h"
#include "../../../deps/minhook/MinHook.h"
#include "../../../shared/common/modules/ModuleManager.h"
#include "../../../shared/common/logging/Logger.h"
#include "../game/classes/Minecraft.h"
#include "../game/classes/MovingObjectPosition.h"
#include "../game/classes/Player.h"
#include "../game/jni/Field.h"
#include "../game/jni/GameInstance.h"
#include "../game/jni/JniRefs.h"
#include "../game/jni/Method.h"
#include "../game/mapping/Mapper.h"
#include "../features/settings/GameChatCommands.h"
#include "../features/visuals/EnemyInfoList.h"
#include "../features/visuals/Target.h"
#include <jni.h>
#include <algorithm>
#include <atomic>
#include <chrono>

typedef double(JNICALL* tStrictMathAtan2)(JNIEnv* env, jclass klass, double x, double y);
static tStrictMathAtan2 g_OrigAtan2 = nullptr;
static void* g_HookedAddr = nullptr;
static std::atomic<bool> g_Installed = false;
static std::atomic<long long> g_LastHookTickMs = 0;
static std::atomic<long long> g_LastFallbackTickMs = 0;
static std::atomic<long long> g_LastChatBreakpointRetryMs = 0;
static std::atomic<bool> g_FirstStrictMathTickLogged = false;
static std::atomic<bool> g_BreakpointCapabilityUnavailableLogged = false;
static jvmtiEnv* g_Jvmti = nullptr;
static jmethodID g_ClickMouseMethod = nullptr;
static jmethodID g_GuiChatKeyTypedMethod = nullptr;
static jmethodID g_GuiSendChatMessageMethod = nullptr;
static jmethodID g_GuiSendChatMessageInternalMethod = nullptr;
static jmethodID g_SendChatMessageMethod = nullptr;
static jlocation g_ClickMouseBreakpointLocation = 0;
static jlocation g_GuiChatKeyTypedBreakpointLocation = 0;
static jlocation g_GuiSendChatMessageBreakpointLocation = 0;
static jlocation g_GuiSendChatMessageInternalBreakpointLocation = 0;
static jlocation g_SendChatMessageBreakpointLocation = 0;
static bool g_BreakpointCallbacksInstalled = false;
static bool g_ClickMouseBreakpointInstalled = false;
static bool g_GuiChatKeyTypedBreakpointInstalled = false;
static bool g_GuiSendChatMessageBreakpointInstalled = false;
static bool g_GuiSendChatMessageInternalBreakpointInstalled = false;
static bool g_SendChatMessageBreakpointInstalled = false;

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

    bool WasKeyPressed(int virtualKey) {
        return (GetAsyncKeyState(virtualKey) & 0x0001) != 0;
    }

    bool IsEnterPressed() {
        return WasKeyPressed(VK_RETURN);
    }

    bool IsTabPressed() {
        return WasKeyPressed(VK_TAB);
    }

    bool IsShiftDown() {
        return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    }

    struct GameChatAutocompleteState {
        std::string baseText;
        std::vector<std::string> suggestions;
        int currentIndex = -1;
    };

    void ResetGameChatAutocomplete(GameChatAutocompleteState& state) {
        state.baseText.clear();
        state.suggestions.clear();
        state.currentIndex = -1;
    }

    bool MatchesGameChatAutocompleteText(const GameChatAutocompleteState& state, const std::string& value) {
        if (value == state.baseText) {
            return true;
        }

        return std::find(state.suggestions.begin(), state.suggestions.end(), value) != state.suggestions.end();
    }

    bool ResolveGuiChatBindings(JNIEnv* env, Class*& guiChatClass, Field*& inputField, Method*& getTextMethod, Method*& setTextMethod) {
        if (!env || !g_Game || !g_Game->IsInitialized()) {
            return false;
        }

        guiChatClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/gui/GuiChat"));
        Class* guiTextFieldClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/gui/GuiTextField"));
        const std::string inputFieldName = Mapper::Get("inputField");
        const std::string textFieldSignature = Mapper::Get("net/minecraft/client/gui/GuiTextField", 2);
        const std::string getTextMethodName = Mapper::Get("getTextFieldText");
        const std::string setTextMethodName = Mapper::Get("setTextFieldText");
        if (!guiChatClass || !guiTextFieldClass || inputFieldName.empty() || textFieldSignature.empty() ||
            getTextMethodName.empty() || setTextMethodName.empty()) {
            return false;
        }

        inputField = guiChatClass->GetField(env, inputFieldName.c_str(), textFieldSignature.c_str());
        getTextMethod = guiTextFieldClass->GetMethod(env, getTextMethodName.c_str(), "()Ljava/lang/String;");
        setTextMethod = guiTextFieldClass->GetMethod(env, setTextMethodName.c_str(), "(Ljava/lang/String;)V");
        return inputField && getTextMethod && setTextMethod;
    }

    void PollGameChatCommands(JNIEnv* env) {
        static bool chatWasOpen = false;
        static std::string lastChatText;
        static GameChatAutocompleteState autocompleteState;

        if (!env || !g_Game || !g_Game->IsInitialized()) {
            return;
        }

        const bool submitKeyPressed = IsEnterPressed();
        const bool tabKeyPressed = IsTabPressed();
        JniLocalRef<jobject> currentScreen(env, Minecraft::GetCurrentScreen(env));

        Class* guiChatClass = nullptr;
        Field* inputField = nullptr;
        Method* getTextMethod = nullptr;
        Method* setTextMethod = nullptr;
        const bool hasGuiBindings = ResolveGuiChatBindings(env, guiChatClass, inputField, getTextMethod, setTextMethod);
        const bool isGuiChatOpen = hasGuiBindings && currentScreen &&
            env->IsInstanceOf(currentScreen.Get(), reinterpret_cast<jclass>(guiChatClass));

        if (isGuiChatOpen) {
            JniLocalRef<jobject> textFieldObject(env, inputField->GetObjectField(env, currentScreen.Get()));
            JniLocalRef<jobject> textObject(
                env,
                textFieldObject ? getTextMethod->CallObjectMethod(env, textFieldObject.Get()) : nullptr);

            const std::string currentChatText = textObject
                ? GameChatCommands::ToUtf8(env, reinterpret_cast<jstring>(textObject.Get()))
                : std::string();
            lastChatText = currentChatText;

            if (!MatchesGameChatAutocompleteText(autocompleteState, currentChatText)) {
                ResetGameChatAutocomplete(autocompleteState);
            }

            if (tabKeyPressed && textFieldObject) {
                const bool reverseCycle = IsShiftDown();
                const bool isCyclingExistingMatch = !autocompleteState.suggestions.empty() &&
                    MatchesGameChatAutocompleteText(autocompleteState, currentChatText);

                if (!isCyclingExistingMatch) {
                    autocompleteState.baseText = currentChatText;
                    autocompleteState.suggestions = GameChatCommands::CollectAutocompleteMatches(currentChatText);
                    if (autocompleteState.suggestions.empty()) {
                        ResetGameChatAutocomplete(autocompleteState);
                    } else {
                        autocompleteState.currentIndex = reverseCycle
                            ? static_cast<int>(autocompleteState.suggestions.size()) - 1
                            : 0;
                    }
                } else if (!autocompleteState.suggestions.empty()) {
                    auto currentIt = std::find(
                        autocompleteState.suggestions.begin(),
                        autocompleteState.suggestions.end(),
                        currentChatText);
                    if (currentIt != autocompleteState.suggestions.end()) {
                        autocompleteState.currentIndex = static_cast<int>(std::distance(autocompleteState.suggestions.begin(), currentIt));
                        if (reverseCycle) {
                            autocompleteState.currentIndex = autocompleteState.currentIndex <= 0
                                ? static_cast<int>(autocompleteState.suggestions.size()) - 1
                                : autocompleteState.currentIndex - 1;
                        } else {
                            autocompleteState.currentIndex = (autocompleteState.currentIndex + 1) % static_cast<int>(autocompleteState.suggestions.size());
                        }
                    } else {
                        autocompleteState.currentIndex = reverseCycle
                            ? static_cast<int>(autocompleteState.suggestions.size()) - 1
                            : 0;
                    }
                }

                if (!autocompleteState.suggestions.empty() &&
                    autocompleteState.currentIndex >= 0 &&
                    autocompleteState.currentIndex < static_cast<int>(autocompleteState.suggestions.size())) {
                    const std::string replacementText = autocompleteState.suggestions[autocompleteState.currentIndex];
                    JniLocalRef<jstring> replacement(env, env->NewStringUTF(replacementText.c_str()));
                    if (replacement) {
                        setTextMethod->CallVoidMethod(env, textFieldObject.Get(), false, replacement.Get());
                        lastChatText = replacementText;
                    }
                }
            }

            if (submitKeyPressed && !currentChatText.empty()) {
                if (GameChatCommands::TryHandleText(env, currentChatText)) {
                    JniLocalRef<jstring> emptyText(env, env->NewStringUTF(""));
                    if (textFieldObject && emptyText) {
                        setTextMethod->CallVoidMethod(env, textFieldObject.Get(), false, emptyText.Get());
                    }
                    Minecraft::DisplayGuiScreen(nullptr, env);
                    lastChatText.clear();
                    ResetGameChatAutocomplete(autocompleteState);
                    OC_LOG_INFOF("GameChat", "Handled chat command via polling while GuiChat was open: %s", currentChatText.c_str());
                }
            }

            chatWasOpen = true;
            return;
        }

        if (chatWasOpen && submitKeyPressed && !lastChatText.empty()) {
            if (GameChatCommands::TryHandleText(env, lastChatText)) {
                lastChatText.clear();
                ResetGameChatAutocomplete(autocompleteState);
                OC_LOG_INFOF("GameChat", "Handled chat command via polling after GuiChat closed: %s", lastChatText.c_str());
            }
        }

        if (!currentScreen) {
            lastChatText.clear();
        }

        chatWasOpen = false;
        if (!currentScreen) {
            ResetGameChatAutocomplete(autocompleteState);
        }
    }

    bool TryHandleGuiChatEnter(JNIEnv* env, jthread thread) {
        if (!env || !thread || !g_Game || !g_Game->IsInitialized() || !g_Jvmti) {
            return false;
        }

        if (!g_Game->HasLocalVariableAccessCapability()) {
            return false;
        }

        jint keyCode = 0;
        if (g_Jvmti->GetLocalInt(thread, 0, 2, &keyCode) != JVMTI_ERROR_NONE) {
            return false;
        }

        if (keyCode != 28 && keyCode != 156) {
            return false;
        }

        jobject rawGuiChat = nullptr;
        if (g_Jvmti->GetLocalObject(thread, 0, 0, &rawGuiChat) != JVMTI_ERROR_NONE || !rawGuiChat) {
            return false;
        }

        JniLocalRef<jobject> guiChatObject(env, rawGuiChat);
        Class* guiChatClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/gui/GuiChat"));
        Class* guiTextFieldClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/gui/GuiTextField"));
        const std::string inputFieldName = Mapper::Get("inputField");
        const std::string textFieldSignature = Mapper::Get("net/minecraft/client/gui/GuiTextField", 2);
        const std::string getTextMethodName = Mapper::Get("getTextFieldText");
        const std::string setTextMethodName = Mapper::Get("setTextFieldText");
        if (!guiChatClass || !guiTextFieldClass || inputFieldName.empty() || textFieldSignature.empty() ||
            getTextMethodName.empty() || setTextMethodName.empty()) {
            return false;
        }

        Field* inputField = guiChatClass->GetField(env, inputFieldName.c_str(), textFieldSignature.c_str());
        Method* getTextMethod = guiTextFieldClass->GetMethod(env, getTextMethodName.c_str(), "()Ljava/lang/String;");
        Method* setTextMethod = guiTextFieldClass->GetMethod(env, setTextMethodName.c_str(), "(Ljava/lang/String;)V");
        if (!inputField || !getTextMethod) {
            return false;
        }

        JniLocalRef<jobject> textFieldObject(env, inputField->GetObjectField(env, guiChatObject.Get()));
        if (!textFieldObject) {
            return false;
        }

        JniLocalRef<jobject> textObject(env, getTextMethod->CallObjectMethod(env, textFieldObject.Get()));
        if (!textObject) {
            return false;
        }

        const bool handled = GameChatCommands::TryHandle(env, reinterpret_cast<jstring>(textObject.Get()));
        if (!handled) {
            return false;
        }

        if (setTextMethod) {
            JniLocalRef<jstring> emptyText(env, env->NewStringUTF(""));
            if (emptyText) {
                setTextMethod->CallVoidMethod(env, textFieldObject.Get(), false, emptyText.Get());
            }
        }

        Minecraft::DisplayGuiScreen(nullptr, env);
        return true;
    }

    void JNICALL BreakpointCallback(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jmethodID method, jlocation location) {
        (void)location;
        (void)jvmti;

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

        if (method == g_GuiChatKeyTypedMethod) {
            JniLocalFrame frame(env, 64);
            if (frame.IsActive() && g_Jvmti) {
                if (TryHandleGuiChatEnter(env, thread)) {
                    if (g_Game && g_Game->HasForceEarlyReturnCapability()) {
                        g_Jvmti->ForceEarlyReturnVoid(thread);
                    }
                }
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
            }
            return;
        }

        if (method == g_GuiSendChatMessageMethod || method == g_GuiSendChatMessageInternalMethod) {
            JniLocalFrame frame(env, 32);
            if (!frame.IsActive() || !g_Jvmti || !g_Game || !g_Game->HasLocalVariableAccessCapability()) {
                return;
            }

            jobject rawMessage = nullptr;
            if (g_Jvmti->GetLocalObject(thread, 0, 1, &rawMessage) == JVMTI_ERROR_NONE && rawMessage) {
                JniLocalRef<jobject> messageObject(env, rawMessage);
                if (GameChatCommands::TryHandle(env, reinterpret_cast<jstring>(messageObject.Get()))) {
                    if (g_Game->HasForceEarlyReturnCapability()) {
                        g_Jvmti->ForceEarlyReturnVoid(thread);
                    }
                }
            }

            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return;
        }

        if (method == g_SendChatMessageMethod) {
            JniLocalFrame frame(env, 32);
            if (!frame.IsActive() || !g_Jvmti || !g_Game || !g_Game->HasLocalVariableAccessCapability()) {
                return;
            }

            jobject rawMessage = nullptr;
            if (g_Jvmti->GetLocalObject(thread, 0, 1, &rawMessage) == JVMTI_ERROR_NONE && rawMessage) {
                JniLocalRef<jobject> messageObject(env, rawMessage);
                if (GameChatCommands::TryHandle(env, reinterpret_cast<jstring>(messageObject.Get()))) {
                    if (g_Game->HasForceEarlyReturnCapability()) {
                        g_Jvmti->ForceEarlyReturnVoid(thread);
                    }
                }
            }

            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return;
        }
    }

    bool EnsureBreakpointCallbacks() {
        if (!g_Jvmti || !g_Game || !g_Game->HasBreakpointEventsCapability()) {
            return false;
        }

        if (!g_BreakpointCallbacksInstalled) {
            jvmtiEventCallbacks callbacks{};
            callbacks.Breakpoint = &BreakpointCallback;

            const jvmtiError setCallbacksError = g_Jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
            if (setCallbacksError != JVMTI_ERROR_NONE) {
                OC_LOG_ERRORF("GameThreadHook", "SetEventCallbacks failed with JVMTI error %d.", static_cast<int>(setCallbacksError));
                return false;
            }

            g_BreakpointCallbacksInstalled = true;
        }

        const jvmtiError notificationError =
            g_Jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
        if (notificationError != JVMTI_ERROR_NONE) {
            OC_LOG_ERRORF("GameThreadHook", "SetEventNotificationMode failed with JVMTI error %d.", static_cast<int>(notificationError));
            return false;
        }

        return true;
    }

    bool InitializeClickMouseBreakpoint(bool logFailures = false) {
        if (!g_Game || !g_Game->IsInitialized()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "ClickMouse breakpoint init skipped because game is not initialized.");
            }
            return false;
        }

        JNIEnv* env = g_Game->GetCurrentEnv();
        g_Jvmti = g_Game->GetJVMTI();
        if (!env || !g_Jvmti) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "ClickMouse breakpoint init failed because env or JVMTI is missing.");
            }
            return false;
        }

        if (!EnsureBreakpointCallbacks()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "ClickMouse breakpoint init failed because breakpoint callbacks could not be enabled.");
            }
            return false;
        }

        const std::string minecraftClassName = Mapper::Get("net/minecraft/client/Minecraft");
        const std::string clickMouseName = Mapper::Get("clickMouse");
        if (minecraftClassName.empty() || clickMouseName.empty()) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "ClickMouse mapping missing. class='%s' method='%s'", minecraftClassName.c_str(), clickMouseName.c_str());
            }
            return false;
        }

        Class* minecraftClass = g_Game->FindClass(minecraftClassName);
        if (!minecraftClass) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "ClickMouse class lookup failed for %s.", minecraftClassName.c_str());
            }
            return false;
        }

        g_ClickMouseMethod = env->GetMethodID(reinterpret_cast<jclass>(minecraftClass), clickMouseName.c_str(), "()V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        if (!g_ClickMouseMethod) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "ClickMouse method lookup failed for %s.%s()V.", minecraftClassName.c_str(), clickMouseName.c_str());
            }
            return false;
        }

        if (!SetBreakpointAtMethodStart(g_ClickMouseMethod, g_ClickMouseBreakpointLocation)) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "ClickMouse breakpoint install failed for %s.%s()V.", minecraftClassName.c_str(), clickMouseName.c_str());
            }
            g_ClickMouseMethod = nullptr;
            return false;
        }

        g_ClickMouseBreakpointInstalled = true;
        return true;
    }

    bool InitializeGuiChatKeyTypedBreakpoint(bool logFailures = false) {
        if (!g_Game || !g_Game->IsInitialized()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "GuiChat keyTyped breakpoint init skipped because game is not initialized.");
            }
            return false;
        }

        JNIEnv* env = g_Game->GetCurrentEnv();
        g_Jvmti = g_Game->GetJVMTI();
        if (!env || !g_Jvmti) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "GuiChat keyTyped breakpoint init failed because env or JVMTI is missing.");
            }
            return false;
        }

        if (!EnsureBreakpointCallbacks()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "GuiChat keyTyped breakpoint init failed because breakpoint callbacks could not be enabled.");
            }
            return false;
        }

        const std::string guiChatClassName = Mapper::Get("net/minecraft/client/gui/GuiChat");
        const std::string guiChatKeyTypedName = Mapper::Get("guiChatKeyTyped");
        if (guiChatClassName.empty() || guiChatKeyTypedName.empty()) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "GuiChat keyTyped mapping missing. class='%s' method='%s'", guiChatClassName.c_str(), guiChatKeyTypedName.c_str());
            }
            return false;
        }

        Class* guiChatClass = g_Game->FindClass(guiChatClassName);
        if (!guiChatClass) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "GuiChat class lookup failed for %s.", guiChatClassName.c_str());
            }
            return false;
        }

        g_GuiChatKeyTypedMethod = env->GetMethodID(
            reinterpret_cast<jclass>(guiChatClass),
            guiChatKeyTypedName.c_str(),
            "(CI)V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        if (!g_GuiChatKeyTypedMethod) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "GuiChat keyTyped method lookup failed for %s.%s(CI)V.", guiChatClassName.c_str(), guiChatKeyTypedName.c_str());
            }
            return false;
        }

        if (!SetBreakpointAtMethodStart(g_GuiChatKeyTypedMethod, g_GuiChatKeyTypedBreakpointLocation)) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "GuiChat keyTyped breakpoint install failed for %s.%s(CI)V.", guiChatClassName.c_str(), guiChatKeyTypedName.c_str());
            }
            g_GuiChatKeyTypedMethod = nullptr;
            return false;
        }

        g_GuiChatKeyTypedBreakpointInstalled = true;
        return true;
    }

    bool InitializeGuiSendChatBreakpoints(bool logFailures = false) {
        if (!g_Game || !g_Game->IsInitialized()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "GuiScreen sendChat breakpoint init skipped because game is not initialized.");
            }
            return false;
        }

        JNIEnv* env = g_Game->GetCurrentEnv();
        g_Jvmti = g_Game->GetJVMTI();
        if (!env || !g_Jvmti) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "GuiScreen sendChat breakpoint init failed because env or JVMTI is missing.");
            }
            return false;
        }

        if (!EnsureBreakpointCallbacks()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "GuiScreen sendChat breakpoint init failed because breakpoint callbacks could not be enabled.");
            }
            return false;
        }

        const std::string guiScreenClassName = Mapper::Get("net/minecraft/client/gui/GuiScreen");
        const std::string guiSendChatMessageName = Mapper::Get("guiSendChatMessage");
        const std::string guiSendChatMessageInternalName = Mapper::Get("guiSendChatMessageInternal");
        if (guiScreenClassName.empty() || guiSendChatMessageName.empty() || guiSendChatMessageInternalName.empty()) {
            if (logFailures) {
                OC_LOG_ERRORF(
                    "GameThreadHook",
                    "GuiScreen sendChat mapping missing. class='%s' public='%s' internal='%s'",
                    guiScreenClassName.c_str(),
                    guiSendChatMessageName.c_str(),
                    guiSendChatMessageInternalName.c_str());
            }
            return false;
        }

        Class* guiScreenClass = g_Game->FindClass(guiScreenClassName);
        if (!guiScreenClass) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "GuiScreen class lookup failed for %s.", guiScreenClassName.c_str());
            }
            return false;
        }

        g_GuiSendChatMessageMethod = env->GetMethodID(
            reinterpret_cast<jclass>(guiScreenClass),
            guiSendChatMessageName.c_str(),
            "(Ljava/lang/String;)V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        g_GuiSendChatMessageInternalMethod = env->GetMethodID(
            reinterpret_cast<jclass>(guiScreenClass),
            guiSendChatMessageInternalName.c_str(),
            "(Ljava/lang/String;Z)V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        if (logFailures && !g_GuiSendChatMessageMethod && !g_GuiSendChatMessageInternalMethod) {
            OC_LOG_ERRORF(
                "GameThreadHook",
                "GuiScreen sendChat method lookup failed for %s.%s(Ljava/lang/String;)V and %s.%s(Ljava/lang/String;Z)V.",
                guiScreenClassName.c_str(),
                guiSendChatMessageName.c_str(),
                guiScreenClassName.c_str(),
                guiSendChatMessageInternalName.c_str());
        }

        bool installedAny = false;
        if (g_GuiSendChatMessageMethod &&
            SetBreakpointAtMethodStart(g_GuiSendChatMessageMethod, g_GuiSendChatMessageBreakpointLocation)) {
            g_GuiSendChatMessageBreakpointInstalled = true;
            installedAny = true;
        } else {
            g_GuiSendChatMessageMethod = nullptr;
        }

        if (g_GuiSendChatMessageInternalMethod &&
            SetBreakpointAtMethodStart(g_GuiSendChatMessageInternalMethod, g_GuiSendChatMessageInternalBreakpointLocation)) {
            g_GuiSendChatMessageInternalBreakpointInstalled = true;
            installedAny = true;
        } else {
            g_GuiSendChatMessageInternalMethod = nullptr;
        }

        return installedAny;
    }

    bool InitializeSendChatMessageBreakpoint(bool logFailures = false) {
        if (!g_Game || !g_Game->IsInitialized()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "EntityPlayerSP sendChat breakpoint init skipped because game is not initialized.");
            }
            return false;
        }

        JNIEnv* env = g_Game->GetCurrentEnv();
        g_Jvmti = g_Game->GetJVMTI();
        if (!env || !g_Jvmti) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "EntityPlayerSP sendChat breakpoint init failed because env or JVMTI is missing.");
            }
            return false;
        }

        if (!EnsureBreakpointCallbacks()) {
            if (logFailures) {
                OC_LOG_ERROR("GameThreadHook", "EntityPlayerSP sendChat breakpoint init failed because breakpoint callbacks could not be enabled.");
            }
            return false;
        }

        const std::string playerClassName = Mapper::Get("net/minecraft/client/entity/EntityPlayerSP");
        const std::string sendChatMessageName = Mapper::Get("sendChatMessage");
        if (playerClassName.empty() || sendChatMessageName.empty()) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "EntityPlayerSP sendChat mapping missing. class='%s' method='%s'", playerClassName.c_str(), sendChatMessageName.c_str());
            }
            return false;
        }

        Class* playerClass = g_Game->FindClass(playerClassName);
        if (!playerClass) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "EntityPlayerSP class lookup failed for %s.", playerClassName.c_str());
            }
            return false;
        }

        g_SendChatMessageMethod = env->GetMethodID(
            reinterpret_cast<jclass>(playerClass),
            sendChatMessageName.c_str(),
            "(Ljava/lang/String;)V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        if (!g_SendChatMessageMethod) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "EntityPlayerSP sendChat method lookup failed for %s.%s(Ljava/lang/String;)V.", playerClassName.c_str(), sendChatMessageName.c_str());
            }
            return false;
        }

        if (!SetBreakpointAtMethodStart(g_SendChatMessageMethod, g_SendChatMessageBreakpointLocation)) {
            if (logFailures) {
                OC_LOG_ERRORF("GameThreadHook", "EntityPlayerSP sendChat breakpoint install failed for %s.%s(Ljava/lang/String;)V.", playerClassName.c_str(), sendChatMessageName.c_str());
            }
            g_SendChatMessageMethod = nullptr;
            return false;
        }

        g_SendChatMessageBreakpointInstalled = true;
        return true;
    }

    void EnsureChatBreakpointsInstalled() {
        if (!g_Game || !g_Game->HasBreakpointEventsCapability()) {
            if (!g_BreakpointCapabilityUnavailableLogged.exchange(true, std::memory_order_relaxed)) {
                OC_LOG_WARNING("GameThreadHook", "JVMTI breakpoint capability is unavailable. Falling back to polling for chat commands.");
            }
            return;
        }

        const long long nowMs = NowMs();
        const long long lastRetryMs = g_LastChatBreakpointRetryMs.load(std::memory_order_relaxed);
        if ((nowMs - lastRetryMs) < 500) {
            return;
        }

        g_LastChatBreakpointRetryMs.store(nowMs, std::memory_order_relaxed);

        if (!g_GuiChatKeyTypedBreakpointInstalled && InitializeGuiChatKeyTypedBreakpoint()) {
            OC_LOG_INFO("GameChat", "Installed GuiChat.keyTyped breakpoint.");
        }

        if ((!g_GuiSendChatMessageBreakpointInstalled || !g_GuiSendChatMessageInternalBreakpointInstalled) &&
            InitializeGuiSendChatBreakpoints()) {
            OC_LOG_INFO("GameChat", "Installed GuiScreen sendChatMessage breakpoint.");
        }

        if (!g_SendChatMessageBreakpointInstalled && InitializeSendChatMessageBreakpoint()) {
            OC_LOG_INFO("GameChat", "Installed EntityPlayerSP.sendChatMessage breakpoint.");
        }
    }

    void ShutdownBreakpoints() {
        if (!g_Jvmti) {
            g_ClickMouseMethod = nullptr;
            g_GuiChatKeyTypedMethod = nullptr;
            g_GuiSendChatMessageMethod = nullptr;
            g_GuiSendChatMessageInternalMethod = nullptr;
            g_SendChatMessageMethod = nullptr;
            g_BreakpointCallbacksInstalled = false;
            g_ClickMouseBreakpointInstalled = false;
            g_GuiChatKeyTypedBreakpointInstalled = false;
            g_GuiSendChatMessageBreakpointInstalled = false;
            g_GuiSendChatMessageInternalBreakpointInstalled = false;
            g_SendChatMessageBreakpointInstalled = false;
            return;
        }

        if (g_ClickMouseBreakpointInstalled && g_ClickMouseMethod) {
            g_Jvmti->ClearBreakpoint(g_ClickMouseMethod, g_ClickMouseBreakpointLocation);
        }
        if (g_GuiChatKeyTypedBreakpointInstalled && g_GuiChatKeyTypedMethod) {
            g_Jvmti->ClearBreakpoint(g_GuiChatKeyTypedMethod, g_GuiChatKeyTypedBreakpointLocation);
        }
        if (g_GuiSendChatMessageBreakpointInstalled && g_GuiSendChatMessageMethod) {
            g_Jvmti->ClearBreakpoint(g_GuiSendChatMessageMethod, g_GuiSendChatMessageBreakpointLocation);
        }
        if (g_GuiSendChatMessageInternalBreakpointInstalled && g_GuiSendChatMessageInternalMethod) {
            g_Jvmti->ClearBreakpoint(g_GuiSendChatMessageInternalMethod, g_GuiSendChatMessageInternalBreakpointLocation);
        }
        if (g_SendChatMessageBreakpointInstalled && g_SendChatMessageMethod) {
            g_Jvmti->ClearBreakpoint(g_SendChatMessageMethod, g_SendChatMessageBreakpointLocation);
        }

        g_Jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);

        jvmtiEventCallbacks callbacks{};
        g_Jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        g_ClickMouseMethod = nullptr;
        g_GuiChatKeyTypedMethod = nullptr;
        g_GuiSendChatMessageMethod = nullptr;
        g_GuiSendChatMessageInternalMethod = nullptr;
        g_SendChatMessageMethod = nullptr;
        g_BreakpointCallbacksInstalled = false;
        g_ClickMouseBreakpointInstalled = false;
        g_GuiChatKeyTypedBreakpointInstalled = false;
        g_GuiSendChatMessageBreakpointInstalled = false;
        g_GuiSendChatMessageInternalBreakpointInstalled = false;
        g_SendChatMessageBreakpointInstalled = false;
        g_Jvmti = nullptr;
    }
}

double JNICALL StrictMathAtan2Hook(JNIEnv* env, jclass klass, double x, double y)
{
    if (env && klass) {
        g_LastHookTickMs.store(NowMs(), std::memory_order_relaxed);
        if (!g_FirstStrictMathTickLogged.exchange(true, std::memory_order_relaxed)) {
            OC_LOG_INFO("GameThreadHook", "StrictMath hook tick reached the runtime thread.");
        }
        JniLocalFrame frame(env, 512);
        if (frame.IsActive()) {
            EnsureChatBreakpointsInstalled();
            PollGameChatCommands(env);
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
    g_LastChatBreakpointRetryMs.store(0, std::memory_order_relaxed);
    g_BreakpointCapabilityUnavailableLogged.store(false, std::memory_order_relaxed);

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
        OC_LOG_ERROR("GameThreadHook", "Failed to create StrictMath atan2 hook.");
        return false;
    }

    if (MH_EnableHook(pAtan2) != MH_OK) {
        OC_LOG_ERROR("GameThreadHook", "Failed to enable StrictMath atan2 hook.");
        return false;
    }

    g_HookedAddr = pAtan2;
    g_Installed.store(true, std::memory_order_relaxed);
    bool clickInstalled = false;
    bool keyTypedInstalled = false;
    bool guiSendInstalled = false;
    bool playerSendInstalled = false;
    if (g_Game && g_Game->HasBreakpointEventsCapability()) {
        clickInstalled = InitializeClickMouseBreakpoint(true);
        keyTypedInstalled = InitializeGuiChatKeyTypedBreakpoint(true);
        guiSendInstalled = InitializeGuiSendChatBreakpoints(true);
        playerSendInstalled = InitializeSendChatMessageBreakpoint(true);
    }
    OC_LOG_INFOF(
        "GameThreadHook",
        "Initial breakpoint state: click=%d keyTyped=%d guiSend=%d playerSend=%d",
        clickInstalled ? 1 : 0,
        keyTypedInstalled ? 1 : 0,
        guiSendInstalled ? 1 : 0,
        playerSendInstalled ? 1 : 0);
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
    g_LastChatBreakpointRetryMs.store(0, std::memory_order_relaxed);
    g_FirstStrictMathTickLogged.store(false, std::memory_order_relaxed);
    g_BreakpointCapabilityUnavailableLogged.store(false, std::memory_order_relaxed);
}
