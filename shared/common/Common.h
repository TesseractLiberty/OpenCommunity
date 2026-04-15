#pragma once

// common headers for both frontdoor and backdoor
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <functional>
#include <algorithm>
#include <unordered_map>

#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)
#define PAD(size) char MACRO_CONCAT(_pad_, __LINE__)[size]

// simple singleton pattern
template<typename T>
class Singleton {
public:
    static T* Get() {
        static T instance;
        return &instance;
    }
    
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    
protected:
    Singleton() = default;
    virtual ~Singleton() = default;
};
