# OpenCommunity

<div align="center">
<p>
    <img width="160" src="https://img.shields.io/badge/OpenCommunity-Tesseract-blueviolet?style=for-the-badge&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0id2hpdGUiPjxwYXRoIGQ9Ik0xMiAyTDIgN2wxMCA1IDEwLTV6TTIgMTdsMTAgNSAxMC01TTIgMTJsMTAgNSAxMC01Ii8+PC9zdmc+">
</p>

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D6?style=flat-square&logo=windows&logoColor=white)](https://www.microsoft.com/windows)
[![JNI/JVMTI](https://img.shields.io/badge/JNI%20%2F%20JVMTI-F80000?style=flat-square&logo=openjdk&logoColor=white)](https://docs.oracle.com/javase/8/docs/technotes/guides/jni/)
[![MIT License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)

</div>

OpenCommunity is a JNI/JVMTI-based injection framework for Minecraft Java Edition. It hooks into a running game process, maps obfuscated class and method names at runtime, and runs feature modules directly from the game thread.

## Project Status

This repository is currently an early public source preview. The code is buildable and documented, but the structure is still being actively cleaned up before stable releases are published.

No official binaries are shipped from this repository yet. Build artifacts such as `.exe`, `.dll`, `.pdb`, and intermediate files should stay out of Git unless a future release process intentionally adds packaged builds.

## Architecture

The project is split into three layers that work together:

- **Runtime** - DLL injected into the Minecraft process. Handles JNI/JVMTI game interaction, feature modules, class mapping, hooks, and overlay rendering.
- **Launcher** - Standalone loader application. Finds the Minecraft process, injects the runtime DLL, and provides the configuration UI.
- **Shared** - Common headers and source used by both sides. Contains `ModuleConfig`, `ModuleOption`, `Module`, `ModuleManager`, shared ImGui compatibility, and logging helpers.

```text
OpenCommunity/
|-- runtime/               DLL that gets injected into the game
|   |-- src/
|   |   |-- core/          hooks, bridge, game thread, render hook
|   |   |-- features/      all runtime modules live here
|   |   |-- game/          JNI wrappers, Minecraft classes, mappings
|   |   `-- Main.cpp       DLL entry point
|   `-- runtime.vcxproj
|-- launcher/              loader app, injector, and configuration UI
|   |-- src/
|   |   |-- config/        client/version information
|   |   |-- core/          bridge and injector
|   |   |-- ui/            launcher screens
|   |   `-- utils/         process helpers
|   `-- launcher.vcxproj
|-- shared/                code used by launcher and runtime
|   `-- common/
|       |-- imgui/         shared ImGui compatibility symbols
|       |-- logging/       lightweight debug logger
|       |-- modules/       ModuleOption, Module, ModuleManager
|       |-- Common.h
|       `-- ModuleConfig.h
|-- deps/                  third-party libraries
|   |-- imgui/
|   `-- minhook/
|-- LICENSE
|-- README.md
`-- OpenCommunity.sln
```

### Runtime Flow

```text
Launcher starts
  -> ProcessHelper finds the target Java process
  -> Launcher Bridge creates OpenCommunitySharedMem
  -> Injector loads runtime.dll into the game process
  -> Runtime attaches to the JVM through JNI/JVMTI
  -> Mapper detects the active client mappings
  -> ModuleRegistry registers all modules
  -> GameThreadHook and RenderHook drive ticks and overlay rendering
```

### Component Boundaries

- `launcher` owns process discovery, injection, shared-memory creation, and the configuration UI.
- `runtime` owns JVM attachment, Minecraft wrappers, runtime hooks, feature logic, and overlay rendering.
- `shared` must stay dependency-light and should not include headers from `launcher` or `runtime`.
- Concrete module registration lives in `runtime/src/features/ModuleRegistry.h`, because it depends on the actual feature classes.

## Supported Versions

The mapping system supports the following Minecraft Java Edition 1.8.x configurations:

| Version | Client | Identifier |
|---------|--------|------------|
| 1.8.x | Badlion | `BADLION` |
| 1.8.x | Forge | `FORGE_1_8` |
| 1.8.x | Feather | `FEATHER_1_8` |
| 1.8.x | Lunar | `LUNAR` |

Each client has its own set of obfuscated-to-readable name mappings, handled automatically by the `Mapper` class.

## Modules

All available modules organized by category.

**Combat** - AutoClicker, ArmorFilter, ArmorSwap, AutoGapple, NoHitDelay

**Movement** - NoJumpDelay

**Visuals** - ArrayList, DamageIndicator, Target, HideClans, Nametags

**Render** - HUD

## Module System

Modules are built from three shared primitives:

- `ModuleOption` - describes UI/config options such as toggles, sliders, combos, colors, text inputs, and buttons.
- `Module` - base class for feature lifecycle, config sync, keybinds, tags, overlay rendering, and active-use state.
- `ModuleManager` - central registry that handles module registration, keybind processing, config sync, ticking, and overlay rendering.

## Registering Modules

All concrete module registration happens in `runtime/src/features/ModuleRegistry.h`. When you create a new module, add it there so the system picks it up at startup.

```cpp
#pragma once

#include "../../../shared/common/modules/ModuleManager.h"

#include "combat/AutoClicker.h"
#include "combat/ArmorFilter.h"
#include "combat/ArmorSwap.h"
#include "combat/AutoGapple.h"
#include "combat/NoHitDelay.h"
#include "movement/NoJumpDelay.h"
#include "visuals/ArrayList.h"
#include "visuals/DamageIndicator.h"
#include "visuals/Nametags.h"
#include "visuals/Target.h"
#include "visuals/HideClans.h"

#include <memory>

namespace ModuleRegistry {
    inline void RegisterAll(ModuleManager& modules) {
        modules.RegisterModule(std::make_shared<AutoClicker>());
        modules.RegisterModule(std::make_shared<ArmorFilter>());
        modules.RegisterModule(std::make_shared<ArmorSwap>());
        modules.RegisterModule(std::make_shared<AutoGapple>());
        modules.RegisterModule(std::make_shared<NoHitDelay>());
        modules.RegisterModule(std::make_shared<Target>());
        modules.RegisterModule(std::make_shared<HideClans>());
        modules.RegisterModule(std::make_shared<NoJumpDelay>());
        modules.RegisterModule(std::make_shared<ArrayList>());
        modules.RegisterModule(std::make_shared<DamageIndicator>());
        modules.RegisterModule(std::make_shared<Nametags>());
    }

    inline void RegisterAll() {
        RegisterAll(*ModuleManager::Get());
    }
}
```

### Adding A New Module

1. Create your module header, for example `runtime/src/features/movement/Sprint.h`:

```cpp
#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"

class Sprint : public Module {
public:
    MODULE_INFO(Sprint, "Sprint", "Auto sprint when moving.", ModuleCategory::Movement) {
        AddOption(ModuleOption::Toggle("Only Forward", true));
        AddOption(ModuleOption::SliderFloat("Speed", 1.0f, 0.5f, 2.0f));
    }

    void SyncToConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        // Write module values into ModuleConfig here.
    }

    void SyncFromConfig(void* configPtr) override {
        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        // Read module values from ModuleConfig here.
    }

    bool IsSynchronous() const override {
        return true;
    }

    void TickSynchronous(void* env) override {
        if (!IsEnabled() || !env) {
            return;
        }

        // env is a JNIEnv* when called from the game thread.
    }
};
```

2. Add the include and registration call in `runtime/src/features/ModuleRegistry.h`:

```cpp
#include "movement/Sprint.h"

// inside ModuleRegistry::RegisterAll(ModuleManager& modules):
modules.RegisterModule(std::make_shared<Sprint>());
```

The `ModuleManager` singleton handles the rest: ticking, config sync, keybinds, UI rendering, and overlay rendering.

### Module API

| Member | Description |
|--------|-------------|
| `MODULE_INFO(Class, Name, Desc, Category)` | Macro that sets up the constructor |
| `AddOption(...)` | Registers a configurable option |
| `IsEnabled()` / `SetEnabled()` | Check or change the module state |
| `SupportsKeybind()` | Controls whether the module can be toggled by keybind |
| `IsSynchronous()` | Return `true` if the module needs to tick on the game thread |
| `TickSynchronous(void* env)` | Game thread callback. `env` is a `JNIEnv*` |
| `Tick()` | Non-game-thread tick for logic that does not need JNI |
| `RenderOverlay(drawList, width, height)` | Overlay rendering callback |
| `SyncToConfig()` / `SyncFromConfig()` | Serialize to/from the shared `ModuleConfig` |
| `GetTag()` | Returns the string shown in the ArrayList HUD |
| `MarkInUse(ms)` / `ClearInUse()` | Tracks whether the module is actively doing something |
| `ShouldRenderOption(index)` | Allows a module to hide/show options dynamically |
| `OnOptionEdited(index)` | Callback fired when an option is changed in the UI |

### Option Types

| Type | Constructor |
|------|-------------|
| Toggle | `ModuleOption::Toggle("Name", false)` |
| Read-only Toggle | `ModuleOption::ToggleReadOnly("Name", false)` |
| Int Slider | `ModuleOption::SliderInt("Name", defaultValue, min, max)` |
| Float Slider | `ModuleOption::SliderFloat("Name", defaultValue, min, max)` |
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

Minecraft obfuscates its class, field, and method names. Different clients can ship different mappings, so the `Mapper` class translates readable names to their obfuscated counterparts for the active game version.

```cpp
// Method or field name
std::string name = Mapper::Get("getHealth");

// Class signature
std::string sig = Mapper::Get("net/minecraft/scoreboard/ScorePlayerTeam", 2);
```

The mapper is initialized at startup based on the detected Minecraft version and client. After that, game wrappers use `Mapper::Get()` internally so they work across Badlion, Forge, Feather, and Lunar without feature-specific mapping code.

## Game Classes

The runtime wraps important Minecraft classes through JNI:

| Class | Description |
|-------|-------------|
| `Minecraft` | Main game instance: player, world, timer |
| `Player` | Player entities: name, health, position, bounding box, team/clan data |
| `World` | World instance: player list and world state |
| `Scoreboard` | Scoreboard system: teams, objectives, scores |
| `Team` | Team data: prefix, suffix, registered name |
| `AxisAlignedBB` | Bounding boxes and hitbox manipulation |
| `ActiveRenderInfo` | Active render information |
| `Container` | Inventory containers |
| `InventoryPlayer` | Player inventory |
| `ItemStack` | Item instances |
| `ItemArmor` | Armor items |
| `Slot` | Inventory slots |
| `GuiScreen` | GUI screens |
| `KeyBinding` | Keybinds |
| `Timer` | Game timer and render partial ticks |
| `PlayerController` | Player controller: attacking and interactions |
| `MovingObjectPosition` | Raytrace and crosshair target |
| `PacketClientStatus` | Client status packets |
| `PotionEffect` | Potion effect data |
| `RenderHelper` | Render helper utilities |
| `RenderItem` | Item rendering |
| `RenderManager` | Render manager instance |

## JNI Helpers

The runtime includes small RAII helpers for JNI lifetime management:

| Helper | Description |
|--------|-------------|
| `JniLocalFrame` | Opens and closes a JNI local reference frame safely |
| `JniLocalRef<T>` | Owns a JNI local reference and deletes it automatically |

These helpers live in `runtime/src/game/jni/JniRefs.h` and reduce manual `DeleteLocalRef` mistakes in game-thread code.

## Hooks

| Hook | Description |
|------|-------------|
| `GameThreadHook` | Hooks the main game thread for synchronous module ticking with a valid `JNIEnv` |
| `RenderHook` | Hooks the render pipeline for overlay rendering |
| `Bridge` | Establishes the shared memory connection between launcher and runtime |

The flow is:

```text
GameThreadHook fires
  -> ModuleManager::TickSynchronousAll(env)
  -> each synchronous module receives TickSynchronous(JNIEnv*)

RenderHook fires
  -> ModuleManager::RenderOverlayAll(drawList, screenWidth, screenHeight)
  -> visual modules render overlays
```

## Shared Memory / IPC

The launcher and runtime communicate through a named shared memory region. Both sides map the same chunk of memory, `OpenCommunitySharedMem`, containing a versioned `ModuleConfig` struct.

```text
Launcher UI  <== shared memory ==>  Runtime DLL
              OpenCommunitySharedMem
```

The launcher writes module settings to `ModuleConfig` when you change things in the UI. The runtime reads from `ModuleConfig` every tick through `ModuleManager::SyncAllFromConfig()`. Both sides use their own `Bridge` class to initialize and access the mapping.

`ModuleConfig` includes a magic value, schema version, and struct size so each side can reject or reset incompatible shared memory layouts instead of silently reading invalid data.

## Injection Flow

```text
Launcher starts
  -> ProcessHelper finds javaw.exe or the target Minecraft process
  -> Bridge sets up shared memory
  -> Injector injects runtime.dll
  -> Runtime DLL entry point runs
      -> attaches to JVM/JVMTI
      -> initializes mappings
      -> registers modules through ModuleRegistry
      -> GameThreadHook hooks synchronous ticks
      -> RenderHook hooks overlay rendering
      -> modules start ticking and syncing
```

The launcher also handles runtime DLL loading/deobfuscation before injection so the payload can be prepared at runtime.

## Dependencies

The following runtimes and SDKs must be installed before building or running:

| Dependency                                                                    | Download |
|-------------------------------------------------------------------------------|----------|
| DirectX End-User Runtime June 2010                                            | [microsoft.com](https://www.microsoft.com/en-US/download/details.aspx?id=6812) |
| .NET Framework 3.5 SP1 runtime                                                | [microsoft.com](https://www.microsoft.com/en-US/download/details.aspx?id=35) |
| Visual C++ Redistributable x64                                                | [aka.ms](https://aka.ms/vc14/vc_redist.x64.exe) |
| .NET SDK 10.0                                                                 | [dotnet.microsoft.com](https://dotnet.microsoft.com/en-us/download/dotnet/thank-you/sdk-10.0.201-windows-x64-installer) |
| Visual Studio .NET Framework SDK / targeting pack, NETFXSDK, provides `cor.h` | Install through the Visual Studio Installer |
| A JDK 17 with JNI headers, `jni.h`, and `JAVA_HOME` pointing to it            | Install through Adoptium, Zulu, or another JDK distribution |

### Bundled Libraries

| Library | Purpose |
|---------|---------|
| [Dear ImGui](https://github.com/ocornut/imgui) | Launcher UI and overlay UI rendering |
| [MinHook](https://github.com/TsudaKageyu/minhook) | Function hooking on x64 |

Keep third-party license files and notices with the dependency source when publishing.

## Setting Up A Workspace

**Requirements:**

- Windows 10/11 x64
- Visual Studio 2022 or later with C++20 support
- Windows SDK 10.0+
- A Visual Studio installation that includes the .NET Framework SDK / targeting pack
- All [dependencies](#dependencies) listed above

**Steps:**

1. Clone the repository.
2. Open `OpenCommunity.sln` in Visual Studio.
3. Set `JAVA_HOME` to a JDK root that contains `include\jni.h`.
4. Set the build configuration to `x64`, either Debug or Release.
5. Build the `launcher` project.
6. Build the `runtime` project.
7. Run `launcher.exe`. It handles process discovery and injection.

The runtime builds as `runtime.dll` and the launcher builds as `launcher.exe`. The launcher needs the runtime DLL to be available at injection time.

### Command Line Build

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  '.\OpenCommunity.sln' `
  /p:Configuration=Debug `
  /p:Platform=x64
```

For Release builds, replace `Debug` with `Release`.

## Project Structure

```text
OpenCommunity/
|-- runtime/
|   |-- pch.h
|   |-- runtime.vcxproj
|   `-- src/
|       |-- Main.cpp
|       |-- core/
|       |   |-- Bridge.h / .cpp
|       |   |-- GameThreadHook.h / .cpp
|       |   `-- RenderHook.h / .cpp
|       |-- features/
|       |   |-- ModuleRegistry.h
|       |   |-- combat/
|       |   |   |-- AutoClicker.h / .cpp
|       |   |   |-- ArmorFilter.h / .cpp
|       |   |   |-- ArmorSwap.h / .cpp
|       |   |   |-- AutoGapple.h / .cpp
|       |   |   `-- NoHitDelay.h / .cpp
|       |   |-- movement/
|       |   |   `-- NoJumpDelay.h / .cpp
|       |   |-- render/
|       |   |   `-- HUD.h / .cpp
|       |   `-- visuals/
|       |       |-- ArrayList.h
|       |       |-- DamageIndicator.h / .cpp
|       |       |-- HideClans.h / .cpp
|       |       |-- Nametags.h / .cpp
|       |       `-- Target.h / .cpp
|       `-- game/
|           |-- classes/
|           |   |-- ActiveRenderInfo.h / .cpp
|           |   |-- AxisAlignedBB.h / .cpp
|           |   |-- Container.h / .cpp
|           |   |-- GuiScreen.h / .cpp
|           |   |-- InventoryPlayer.h / .cpp
|           |   |-- ItemArmor.h / .cpp
|           |   |-- ItemStack.h / .cpp
|           |   |-- KeyBinding.h / .cpp
|           |   |-- Minecraft.h / .cpp
|           |   |-- MovingObjectPosition.h / .cpp
|           |   |-- PacketClientStatus.h / .cpp
|           |   |-- Player.h / .cpp
|           |   |-- PlayerController.h / .cpp
|           |   |-- PotionEffect.h / .cpp
|           |   |-- RenderHelper.h / .cpp
|           |   |-- RenderItem.h / .cpp
|           |   |-- RenderManager.h / .cpp
|           |   |-- Scoreboard.h / .cpp
|           |   |-- Slot.h / .cpp
|           |   |-- Team.h / .cpp
|           |   |-- Timer.h / .cpp
|           |   `-- World.h / .cpp
|           |-- jni/
|           |   |-- Class.h / .cpp
|           |   |-- Field.h / .cpp
|           |   |-- GameInstance.h / .cpp
|           |   |-- JniRefs.h
|           |   `-- Method.h / .cpp
|           `-- mapping/
|               `-- Mapper.h / .cpp
|-- launcher/
|   |-- pch.h
|   |-- launcher.vcxproj
|   `-- src/
|       |-- EntryPoint.cpp
|       |-- Main.cpp
|       |-- config/
|       |   `-- ClientInfo.h
|       |-- core/
|       |   |-- Bridge.h / .cpp
|       |   `-- Injector.h / .cpp
|       |-- ui/
|       |   `-- Screen.h / .cpp
|       `-- utils/
|           `-- ProcessHelper.h
|-- shared/
|   `-- common/
|       |-- Common.h
|       |-- ModuleConfig.h
|       |-- imgui/
|       |   `-- ImGuiCompat.cpp
|       |-- logging/
|       |   `-- Logger.h
|       `-- modules/
|           |-- Module.h
|           |-- ModuleManager.h
|           `-- ModuleOption.h
|-- deps/
|   |-- imgui/
|   `-- minhook/
|-- LICENSE
|-- README.md
`-- OpenCommunity.sln
```

## License

This project is licensed under the MIT License. See `LICENSE` for details.

## Credits

Developed by **Lopes** and thanks to **the entire community** that created or thought of the methods I'm leaking.

*(c) Tesseract Group*
