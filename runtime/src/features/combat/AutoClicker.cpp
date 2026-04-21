#include "pch.h"
#include "AutoClicker.h"

namespace {
    void PostLeftClick(HWND window) {
        PostMessageA(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 0));
        PostMessageA(window, WM_LBUTTONUP, 0, MAKELPARAM(0, 0));
    }
    
    void MoveMouse(int dx, int dy) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(INPUT));
    }
}

bool AutoClicker::InitGameClasses() {
    if (m_GameClassesInit) return true;
    if (m_GameClassesFailed) return false;

    if (!g_Game || !g_Game->IsInitialized()) return false;

    auto* env = g_Game->GetENV();
    if (!env) {
        m_GameClassesFailed = true;
        return false;
    }

    // Find Minecraft class using Mapper
    auto mcClassName = Mapper::Get("net/minecraft/client/Minecraft");
    if (mcClassName.empty()) {
        m_GameClassesFailed = true;
        return false;
    }

    m_MinecraftClass = g_Game->FindClass(mcClassName);
    if (!m_MinecraftClass) {
        m_GameClassesFailed = true;
        return false;
    }

    auto theMinecraftName = Mapper::Get("theMinecraft");
    auto mcClassDesc = Mapper::Get("net/minecraft/client/Minecraft", 2);
    if (!theMinecraftName.empty() && !mcClassDesc.empty()) {
        m_TheMinecraftField = m_MinecraftClass->GetField(env, theMinecraftName.c_str(), mcClassDesc.c_str(), true);
    }

    auto leftClickName = Mapper::Get("leftClickCounter");
    if (!leftClickName.empty()) {
        m_LeftClickCounterField = m_MinecraftClass->GetField(env, leftClickName.c_str(), "I");
    }

    if (!m_TheMinecraftField || !m_LeftClickCounterField) {
        m_GameClassesFailed = true;
        return false;
    }

    m_GameClassesInit = true;
    return true;
}

// SEH wrapper — no C++ objects with destructors allowed here
static void DoResetLeftClickCounter(JNIEnv* env, Class* minecraftClass, Field* theMinecraftField, Field* leftClickCounterField) {
    __try {
        if (env->ExceptionCheck()) env->ExceptionClear();

        jobject mcInstance = theMinecraftField ? theMinecraftField->GetObjectField(env, minecraftClass, true) : nullptr;
        if (!mcInstance) return;

        leftClickCounterField->SetIntField(env, mcInstance, 0);

        env->DeleteLocalRef(mcInstance);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
}

void AutoClicker::ResetLeftClickCounter() {
    if (!m_GameClassesInit || !g_Game) return;

    auto* env = g_Game->GetENV();
    if (!env) return;

    DoResetLeftClickCounter(env, m_MinecraftClass, m_TheMinecraftField, m_LeftClickCounterField);
}

AutoClicker::Clock::duration AutoClicker::NextInterval() {
    int minCps = (std::max)(1, GetMinCps());
    int maxCps = (std::max)(minCps, GetMaxCps());
    
    double sampledCps = (minCps == maxCps)
        ? static_cast<double>(minCps)
        : std::uniform_real_distribution<double>(
            static_cast<double>(minCps), 
            static_cast<double>(maxCps))(m_Rng);
    
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / sampledCps));
}

void AutoClicker::Reset() {
    m_Armed = false;
    m_NextClickTime = Clock::time_point{};
}

void AutoClicker::Tick() { Run(); }

void AutoClicker::Run() {
    if (!IsEnabled()) {
        Reset();
        return;
    }
    
    HWND mcWindow = FindWindowA("LWJGL", nullptr);
    if (!mcWindow) mcWindow = FindWindowA("GLFW30", nullptr);
    if (!mcWindow || GetForegroundWindow() != mcWindow) {
        Reset();
        return;
    }
    
    // Initialize game classes on first run (lazy init via Mapper)
    InitGameClasses();
    
    bool onlyWhileHolding = GetOnlyWhileHolding();
    bool jitter = GetJitter();
    
    bool physicallyHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool shouldClick = !onlyWhileHolding || physicallyHeld;
    if (!shouldClick) {
        Reset();
        return;
    }
    
    // Reset Minecraft's leftClickCounter so rapid clicks are processed
    ResetLeftClickCounter();
    
    auto now = Clock::now();
    if (!m_Armed) {
        m_Armed = true;
        m_NextClickTime = physicallyHeld 
            ? now + NextInterval() 
            : now;
    }
    
    if (m_NextClickTime > now + std::chrono::milliseconds(1)) {
        return;
    }
    
    while (Clock::now() < m_NextClickTime) {
        YieldProcessor();
    }
    
    if (jitter) {
        std::uniform_int_distribution<int> dist(-1, 1);
        MoveMouse(dist(m_Rng), dist(m_Rng));
    }
    
    // Reset again right before click for maximum effectiveness
    ResetLeftClickCounter();
    PostLeftClick(mcWindow);
    MarkInUse(175);

    now = Clock::now();
    auto nextInterval = NextInterval();
    m_NextClickTime += nextInterval;
    
    while (m_NextClickTime <= now) {
        m_NextClickTime += nextInterval;
    }
}
