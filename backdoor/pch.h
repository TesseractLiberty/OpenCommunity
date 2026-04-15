#pragma once

// precompiled header do backdoor
// DLL injetada no javaw

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// std
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <cmath>

// shared
#include "../shared/Common.h"

// vendors - imgui pra arraylist
#include "vendors/imgui/imgui.h"
