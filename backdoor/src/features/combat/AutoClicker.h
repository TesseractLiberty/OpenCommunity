#pragma once

#include <chrono>
#include <random>

class AutoClicker {
public:
    void Run();
    void Reset();

    static AutoClicker* Get() {
        static AutoClicker instance;
        return &instance;
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::duration NextInterval(int minCps, int maxCps);

    std::mt19937 m_Rng{ std::random_device{}() };
    Clock::time_point m_NextClickTime{};
    bool m_Armed = false;
};
