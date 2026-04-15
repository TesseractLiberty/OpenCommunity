#include "pch.h"
#include "AutoClicker.h"
#include "../../core/Bridge.h"
#include "../../config/ModuleConfig.h"

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

AutoClicker::Clock::duration AutoClicker::NextInterval(int minCps, int maxCps) {
    minCps = (std::max)(1, minCps);
    maxCps = (std::max)(minCps, maxCps);
    
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

void AutoClicker::Run() {
    auto* config = Bridge::Get()->GetConfig();
    if (!config || !config->AutoClicker.m_Enabled) {
        Reset();
        return;
    }
    
        HWND mcWindow = FindWindowA("LWJGL", nullptr);
    if (!mcWindow) mcWindow = FindWindowA("GLFW30", nullptr);
    if (!mcWindow || GetForegroundWindow() != mcWindow) {
        Reset();
        return;
    }
    
    bool physicallyHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool shouldClick = !config->AutoClicker.m_OnlyWhileHolding || physicallyHeld;
    if (!shouldClick) {
        Reset();
        return;
    }
    
    config->Modules.m_AutoClicker = true;
    
    auto now = Clock::now();
    if (!m_Armed) {
        m_Armed = true;
        m_NextClickTime = physicallyHeld 
            ? now + NextInterval(config->AutoClicker.m_MinCps, config->AutoClicker.m_MaxCps) 
            : now;
    }
    
    if (m_NextClickTime > now + std::chrono::milliseconds(1)) {
        return;
    }
    
    while (Clock::now() < m_NextClickTime) {
        YieldProcessor();
    }
    
    if (config->AutoClicker.m_Jitter) {
        std::uniform_int_distribution<int> dist(-1, 1);
        MoveMouse(dist(m_Rng), dist(m_Rng));
    }
    
    PostLeftClick(mcWindow);

    // calc next interval
    now = Clock::now();
    auto nextInterval = NextInterval(config->AutoClicker.m_MinCps, config->AutoClicker.m_MaxCps);
    m_NextClickTime += nextInterval;
    
    while (m_NextClickTime <= now) {
        m_NextClickTime += nextInterval;
    }
}
