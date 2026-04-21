#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <random>
#include <ctime>
#include <filesystem>
#include <tlhelp32.h>
#include <psapi.h>

#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

#include "../shared/common/Common.h"

#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_internal.h"
#include "../deps/imgui/imgui_impl_win32.h"
#include "../deps/imgui/imgui_impl_dx11.h"

