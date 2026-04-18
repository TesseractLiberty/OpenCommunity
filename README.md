# OpenCommunity

OpenCommunity is a JNI/JVMTI-based injection framework for Minecraft: Java Edition. It injects a native DLL into the game process, resolves obfuscated mappings at runtime, and runs modules from the live game thread while the frontdoor handles configuration and injection.

## Architecture

The project is split into three layers:

- `backdoor` - injected DLL responsible for hooks, JNI/JVMTI access, mappings, and in-game features.
- `frontdoor` - standalone loader that finds the game, injects the DLL, and renders the configuration UI.
- `shared` - common types used by both sides, including `FeatureManager`, `ModuleConfig`, and module registration.

## Project Layout

```text
OpenCommunity/
|-- backdoor/
|   |-- src/
|   |   |-- core/
|   |   |-- features/
|   |   |   |-- combat/
|   |   |   |-- movement/
|   |   |   |-- render/
|   |   |   `-- visuals/
|   |   `-- game/
|   `-- backdoor.vcxproj
|-- frontdoor/
|   |-- src/
|   |   |-- config/
|   |   |-- core/
|   |   |-- ui/
|   |   `-- utils/
|   `-- frontdoor.vcxproj
|-- shared/
|   `-- common/
|-- deps/
`-- tests/
```

## Module Layout

### Combat

- `AutoClicker`
- `ArmorFilter`
- `ArmorSwap`
- `AutoGapple`
- `NoHitDelay`

### Movement

- `NoJumpDelay`

### Visuals

- `ArrayList`
- `DamageIndicator`
- `Target`
- `HideClans`

### Render

- `HUD`

## Registering Modules

Modules are registered in `shared/common/RegisterModules.h`.

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
    // ...
}
```

## Supported Clients

The mapping layer currently targets these 1.8.x environments:

- `BADLION`
- `FORGE_1_8`
- `FEATHER_1_8`
- `LUNAR`

Each environment uses its own obfuscated-to-readable mapping set through `Mapper`.

## Notes

- The frontdoor writes settings into shared memory through `ModuleConfig`.
- The backdoor reads those settings every tick and applies them in-game.
- Rendering hooks feed module overlays through `FeatureManager::RenderOverlayAll()`.
