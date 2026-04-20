<div align="center">
<p>
    <img width="160" src="https://img.shields.io/badge/OpenCommunity-Tesseract-blueviolet?style=for-the-badge&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0id2hpdGUiPjxwYXRoIGQ9Ik0xMiAyTDIgN2wxMCA1IDEwLTV6TTIgMTdsMTAgNSAxMC01TTIgMTJsMTAgNSAxMC01Ii8+PC9zdmc+">
</p>

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D6?style=flat-square&logo=windows&logoColor=white)](https://www.microsoft.com/windows)
[![JNI/JVMTI](https://img.shields.io/badge/JNI%20%2F%20JVMTI-F80000?style=flat-square&logo=openjdk&logoColor=white)](https://docs.oracle.com/javase/8/docs/technotes/guides/jni/)

</div>

OpenCommunity is a JNI/JVMTI-based injection framework for Minecraft (Java Edition). It hooks into a running game process, maps obfuscated class and method names at runtime, and runs feature modules directly from the game thread.

## Architecture

The project is split into three layers that work together:

- **Backdoor** — DLL injected into the Minecraft process. Handles JNI/JVMTI game interaction, feature modules, class mapping, and overlay rendering.
- **Frontdoor** — Standalone loader application. Finds the Minecraft process, injects the backdoor DLL, and provides the configuration UI.
- **Shared** — Common headers used by both sides. Contains `FeatureManager`, `ModuleConfig`, and module registration.

```
OpenCommunity/
├── backdoor/          DLL that gets injected into the game
│   └── src/
│       ├── core/      hooks, bridge, game thread
│       ├── features/  all the modules live here
│       ├── game/      JNI wrappers for mc classes
│       └── ui/        overlay / imgui rendering
├── frontdoor/         loader app (finds mc, injects, shows UI)
│   └── src/
│       ├── config/    client version info
│       ├── core/      bridge, injector
│       ├── ui/        frontend screens
│       └── utils/     process helpers
├── shared/            stuff both sides need
│   └── common/
│       ├── FeatureManager.h
│       ├── ModuleConfig.h
│       ├── RegisterModules.h
│       └── Common.h
├── deps/              third party libs
│   ├── imgui/
│   └── minhook/
└── tests/
    ├── backdoor/
    └── frontdoor/
```

## Supported Versions

The mapping system supports the following Minecraft (Java Edition) 1.8.x configurations:

| Version | Client | Identifier |
|---------|--------|------------|
| 1.8.x | Badlion | `BADLION` |
| 1.8.x | Forge | `FORGE_1_8` |
| 1.8.x | Feather | `FEATHER_1_8` |
| 1.8.x | Lunar | `LUNAR` |

Each version has its own set of obfuscated-to-readable name mappings, handled automatically by the `Mapper` class.

## Modules

All available modules organized by category.

**Combat** — AutoClicker, ArmorFilter, ArmorSwap, AutoGapple, NoHitDelay

**Movement** — NoJumpDelay

**Visuals** — ArrayList, DamageIndicator, Target, HideClans

**Render** — HUD

## Registering Modules

All module registration happens in `shared/common/RegisterModules.h`. When you create a new module, you add it there so the system picks it up at startup.

```cpp
#include "FeatureManager.h"
#include "../../backdoor/src/features/combat/AutoClicker.h"
#include "../../backdoor/src/features/combat/ArmorFilter.h"
#include "../../backdoor/src/features/combat/ArmorSwap.h"
#include "../../backdoor/src/features/combat/AutoGapple.h"
#include "../../backdoor/src/features/combat/NoHitDelay.h"
#include "../../backdoor/src/features/movement/NoJumpDelay.h"
#include "../../backdoor/src/features/visuals/ArrayList.h"
#include "../../backdoor/src/features/visuals/DamageIndicator.h"
#include "../../backdoor/src/features/visuals/Target.h"
#include "../../backdoor/src/features/visuals/HideClans.h"

inline void RegisterAllModules() {
    auto* fm = FeatureManager::Get();
    fm->RegisterModule(std::make_shared<AutoClicker>());
    fm->RegisterModule(std::make_shared<ArmorFilter>());
    fm->RegisterModule(std::make_shared<ArmorSwap>());
    fm->RegisterModule(std::make_shared<AutoGapple>());
    fm->RegisterModule(std::make_shared<NoHitDelay>());
    fm->RegisterModule(std::make_shared<Target>());
    fm->RegisterModule(std::make_shared<HideClans>());
    fm->RegisterModule(std::make_shared<NoJumpDelay>());
    fm->RegisterModule(std::make_shared<ArrayList>());
    fm->RegisterModule(std::make_shared<DamageIndicator>());
}
```

### Adding a new module

1. Create your module header (e.g. `backdoor/src/features/movement/Sprint.h`):

```cpp
#pragma once
#include "../../../../shared/common/FeatureManager.h"

class Sprint : public Module {
public:
    MODULE_INFO(Sprint, "Sprint", "Auto sprint when moving.", ModuleCategory::Movement) {
        AddOption(ModuleOption::Toggle("Only Forward", true));
        AddOption(ModuleOption::SliderFloat("Speed", 1.0f, 0.5f, 2.0f));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;
        // write your fields to config
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) return;
        // read your fields from config
    }

    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* env) override {
        if (!IsEnabled()) return;
        // your logic here — env is a JNIEnv*
    }
};
```

2. Add the include and registration call in `RegisterModules.h`:

```cpp
#include "../../backdoor/src/features/movement/Sprint.h"

// inside RegisterAllModules():
fm->RegisterModule(std::make_shared<Sprint>());
```

The `FeatureManager` singleton handles the rest — ticking, config sync, keybinds, UI rendering, everything.

### Module API

| Member | Description |
|--------|-------------|
| `MODULE_INFO(Class, Name, Desc, Category)` | Macro that sets up the constructor |
| `AddOption(...)` | Registers a configurable option |
| `IsEnabled()` / `SetEnabled()` | Check or change the module state |
| `IsSynchronous()` | Return `true` if the module needs to tick on the game thread |
| `TickSynchronous(void* env)` | Game thread callback — `env` is a `JNIEnv*` |
| `Tick()` | Non-game-thread tick (for logic that doesn't need JNI) |
| `SyncToConfig()` / `SyncFromConfig()` | Serialize to/from the shared `ModuleConfig` |
| `GetTag()` | Returns the string shown in the ArrayList HUD |
| `MarkInUse(ms)` / `ClearInUse()` | Tracks whether the module is actively doing something |

### Option Types

| Type | Constructor |
|------|-------------|
| Toggle | `ModuleOption::Toggle("Name", false)` |
| Int Slider | `ModuleOption::SliderInt("Name", default, min, max)` |
| Float Slider | `ModuleOption::SliderFloat("Name", default, min, max)` |
| Dropdown | `ModuleOption::Combo("Name", {"A", "B", "C"}, 0)` |
| Color Picker | `ModuleOption::Color("Name", r, g, b, a)` |
| Text Input | `ModuleOption::Text("Name", "default", maxLen)` |
| Button | `ModuleOption::Button("Name", "Label")` |

### Categories

- `ModuleCategory::Combat`
- `ModuleCategory::Movement`
- `ModuleCategory::Visuals`
- `ModuleCategory::Settings`

## Mapping System

Minecraft obfuscates its class and method names, and every version/client has different mappings. The `Mapper` class translates human-readable names to their obfuscated counterparts for the active game version.

```cpp
// Method/field name (Type 1)
std::string name = Mapper::Get("getHealth");  // → "func_110143_aJ"

// Class signature (Type 2)
std::string sig = Mapper::Get("net/minecraft/scoreboard/ScorePlayerTeam", 2);  // → "Lbbl;"
```

The mapper is initialized at startup based on the detected Minecraft version and client. After that, all game classes use `Mapper::Get()` internally so they work across Badlion, Forge, Feather, and Lunar without modification.

## Game Classes

The backdoor wraps all important Minecraft classes through JNI:

| Class | Description |
|-------|-------------|
| `Minecraft` | Main game instance — player, world, timer |
| `Player` | Player entities — name, health, position, bounding box, clan tag |
| `World` | World instance — player list, scoreboard |
| `Scoreboard` | Scoreboard system — teams, objectives, scores |
| `Team` | Team data — prefix, suffix, registered name |
| `AxisAlignedBB` | Bounding boxes — hitbox manipulation |
| `ActiveRenderInfo` | Active render information |
| `Container` | Inventory containers |
| `InventoryPlayer` | Player inventory |
| `ItemStack` | Item instances |
| `ItemArmor` | Armor items |
| `Slot` | Inventory slots |
| `GuiScreen` | GUI screens |
| `KeyBinding` | Keybinds |
| `Timer` | Game timer (render partial ticks) |
| `PlayerController` | Player controller — attacking, interactions |
| `MovingObjectPosition` | Raytrace / crosshair target |
| `PacketClientStatus` | Client status packets |
| `PotionEffect` | Potion effect data |
| `RenderHelper` | Render helper utilities |
| `RenderItem` | Item rendering |
| `RenderManager` | Render manager instance |

## Hooks

| Hook | Description |
|------|-------------|
| `GameThreadHook` | Hooks the main game thread for synchronous module ticking with a valid `JNIEnv` |
| `GL11Hook` | Hooks OpenGL calls for overlay rendering |
| `RenderHook` | Additional render pipeline hooks |
| `Bridge` | Establishes the shared memory connection to the frontdoor |

The flow: `GameThreadHook` fires → `FeatureManager::TickSynchronousAll(env)` → each synchronous module gets its `TickSynchronous()` called with a live `JNIEnv*`. For rendering, `GL11Hook`/`RenderHook` fire on the render thread and call `FeatureManager::RenderOverlayAll()`.

## Shared Memory / IPC

The frontdoor and backdoor communicate through a named shared memory region. Both sides map the same chunk of memory (`OpenCommunitySharedMem`) containing a `ModuleConfig` struct.

```
Frontdoor (UI)  ◄══ shared memory ══►  Backdoor (DLL)
                "OpenCommunitySharedMem"
```

The frontdoor writes module settings to `ModuleConfig` when you change things in the UI. The backdoor reads from `ModuleConfig` every tick via `SyncAllFromConfig()`. Both sides use the `Bridge` class to initialize and access the mapping.

## Injection Flow

```
Frontdoor starts
  → ProcessHelper finds javaw.exe (Minecraft)
  → Injector uses custom manual mapping to inject the backdoor DLL
  → Bridge sets up shared memory
  → Backdoor DLL entry point runs
      → GameThreadHook hooks the game thread
      → GL11Hook / RenderHook hooks the render pipeline
      → RegisterAllModules() registers everything with FeatureManager
      → Modules start ticking
```

The frontdoor also handles DLL obfuscation/deobfuscation before injection so the payload isn't stored in plaintext.

## Dependencies

The following runtimes and SDKs must be installed before building or running:

| Dependency | Download |
|------------|----------|
| DirectX End-User Runtime (June 2010) | [microsoft.com](https://www.microsoft.com/en-US/download/details.aspx?id=6812) |
| .NET Framework 3.5 SP1 runtime | [microsoft.com](https://www.microsoft.com/en-US/download/details.aspx?id=35) |
| Visual C++ Redistributable (x64) | [aka.ms](https://aka.ms/vc14/vc_redist.x64.exe) |
| .NET SDK 10.0 | [dotnet.microsoft.com](https://dotnet.microsoft.com/en-us/download/dotnet/thank-you/sdk-10.0.201-windows-x64-installer) |
| Visual Studio .NET Framework SDK / targeting pack (NETFXSDK, provides `cor.h`) | Install through the Visual Studio Installer |
| A JDK with JNI headers (`jni.h`) and `JAVA_HOME` pointing to it | Install through Adoptium, Zulu, or another JDK distribution |

### Bundled Libraries

| Library | Purpose |
|---------|---------|
| [Dear ImGui](https://github.com/ocornut/imgui) | Overlay UI rendering |
| [MinHook](https://github.com/TsudaKageworked/minhook) | Function hooking (x64) |

## Setting up a Workspace

**Requirements:**
- Windows 10/11 x64
- Visual Studio 2022 (or later) with C++17 support
- Windows SDK 10.0+
- A Visual Studio installation that includes the .NET Framework SDK / targeting pack
- All [dependencies](#dependencies) listed above

**Steps:**

1. Clone the repository.
2. Open the solution in Visual Studio.
3. Set `JAVA_HOME` to a JDK root that contains `include\jni.h`.
4. Set the build configuration to `x64` (Release or Debug).
5. Build the **frontdoor** project first.
6. Build the **backdoor** project.
7. Run the frontdoor — it handles injection automatically.

> The backdoor builds as a DLL and the frontdoor as an EXE. The frontdoor needs the backdoor DLL to be available at injection time.

## Project Structure

```
OpenCommunity/
├── backdoor/
│   ├── pch.h
│   └── src/
│       ├── core/
│       │   ├── Bridge.h / .cpp
│       │   ├── GameThreadHook.h / .cpp
│       │   ├── GL11Hook.h / .cpp
│       │   └── RenderHook.h / .cpp
│       ├── features/
│       │   ├── combat/
│       │   │   ├── AutoClicker.h / .cpp
│       │   │   ├── ArmorFilter.h / .cpp
│       │   │   ├── ArmorSwap.h / .cpp
│       │   │   ├── AutoGapple.h / .cpp
│       │   │   └── NoHitDelay.h / .cpp
│       │   ├── movement/
│       │   │   └── NoJumpDelay.h / .cpp
│       │   ├── render/
│       │   │   └── HUD.h / .cpp
│       │   └── visuals/
│       │       ├── ArrayList.h
│       │       ├── DamageIndicator.h / .cpp
│       │       ├── Target.h / .cpp
│       │       └── HideClans.h / .cpp
│       ├── game/
│       │   ├── classes/
│       │   │   ├── ActiveRenderInfo.h / .cpp
│       │   │   ├── AxisAlignedBB.h / .cpp
│       │   │   ├── Container.h / .cpp
│       │   │   ├── GuiScreen.h / .cpp
│       │   │   ├── InventoryPlayer.h / .cpp
│       │   │   ├── ItemArmor.h / .cpp
│       │   │   ├── ItemStack.h / .cpp
│       │   │   ├── KeyBinding.h / .cpp
│       │   │   ├── Minecraft.h / .cpp
│       │   │   ├── MovingObjectPosition.h / .cpp
│       │   │   ├── PacketClientStatus.h / .cpp
│       │   │   ├── Player.h / .cpp
│       │   │   ├── PlayerController.h / .cpp
│       │   │   ├── PotionEffect.h / .cpp
│       │   │   ├── RenderHelper.h / .cpp
│       │   │   ├── RenderItem.h / .cpp
│       │   │   ├── RenderManager.h / .cpp
│       │   │   ├── Scoreboard.h / .cpp
│       │   │   ├── Slot.h / .cpp
│       │   │   ├── Team.h / .cpp
│       │   │   ├── Timer.h / .cpp
│       │   │   └── World.h / .cpp
│       │   ├── jni/
│       │   │   ├── Class.h / .cpp
│       │   │   ├── Field.h / .cpp
│       │   │   ├── GameInstance.h / .cpp
│       │   │   └── Method.h / .cpp
│       │   └── mapping/
│       │       ├── Mapper.h / .cpp
│       │       └── Mappings.h
│       └── ui/
├── frontdoor/
│   ├── pch.h
│   └── src/
│       ├── EntryPoint.cpp
│       ├── Main.cpp
│       ├── config/
│       │   └── ClientInfo.h
│       ├── core/
│       │   ├── Bridge.h / .cpp
│       │   └── Injector.h / .cpp
│       ├── ui/
│       │   ├── ImGuiCompat.cpp
│       │   └── Screen.h / .cpp
│       └── utils/
│           └── ProcessHelper.h
├── shared/
│   └── common/
│       ├── Common.h
│       ├── FeatureManager.h
│       ├── ModuleConfig.h
│       └── RegisterModules.h
├── deps/
│   ├── imgui/
│   └── minhook/
└── tests/
    ├── backdoor/
    └── frontdoor/
```

## Credits

Developed by **Lopes** & thx to **the entire community** that created or thought of the methods I'm leaking.

*© Tesseract Group*
