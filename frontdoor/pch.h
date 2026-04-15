#pragma once

// precompiled header do frontdoor
// inclui tudo que é usado frequentemente pra acelerar compilação

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// std
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

// d3d11 e imgui
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

// shared
#include "../shared/Common.h"

// vendors (imgui fica aqui)
#include "vendors/imgui/imgui.h"
#include "vendors/imgui/imgui_internal.h"
#include "vendors/imgui/imgui_impl_win32.h"
#include "vendors/imgui/imgui_impl_dx11.h"

