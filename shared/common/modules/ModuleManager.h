#pragma once

#include "Module.h"
#include "../ModuleConfig.h"

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

class ModuleManager {
public:
    using KeybindProcessPredicate = std::function<bool(DWORD)>;
    using KeybindInputBlockPredicate = std::function<bool()>;
    using ModuleToggleCallback = std::function<void(const Module& module, bool enabled)>;

    static ModuleManager* Get() {
        static ModuleManager instance;
        return &instance;
    }

    void SetKeybindProcessPredicate(KeybindProcessPredicate predicate) {
        m_KeybindProcessPredicate = std::move(predicate);
    }

    void SetKeybindInputBlockPredicate(KeybindInputBlockPredicate predicate) {
        m_KeybindInputBlockPredicate = std::move(predicate);
    }

    void SetModuleToggleCallback(ModuleToggleCallback callback) {
        m_ModuleToggleCallback = std::move(callback);
    }

    void RegisterModule(std::shared_ptr<Module> module) {
        if (!module) {
            return;
        }

        auto& categoryModules = m_Modules[module->GetCategory()];
        for (const auto& registeredModule : categoryModules) {
            if (registeredModule && registeredModule->GetName() == module->GetName()) {
                return;
            }
        }

        m_KnownEnabledStates[BuildModuleKey(*module)] = module->IsEnabled();
        categoryModules.push_back(std::move(module));
    }

    const std::vector<std::shared_ptr<Module>>& GetModules(ModuleCategory category) const {
        static const std::vector<std::shared_ptr<Module>> empty;
        auto it = m_Modules.find(category);
        return it != m_Modules.end() ? it->second : empty;
    }

    std::vector<std::shared_ptr<Module>> GetAllModules() const {
        std::vector<std::shared_ptr<Module>> allModules;
        for (const auto& [category, modules] : m_Modules) {
            (void)category;
            for (const auto& module : modules) {
                allModules.push_back(module);
            }
        }
        return allModules;
    }

    void ProcessKeybinds() {
        const std::unordered_map<int, bool> currentKeyStates = CaptureBoundKeyStates();
        const bool canProcessInput = IsKeybindInputAllowed() && !IsKeybindInputBlocked();

        if (!canProcessInput) {
            m_KeybindDownStates = currentKeyStates;
            return;
        }

        for (auto& [category, modules] : m_Modules) {
            (void)category;
            for (auto& module : modules) {
                if (!module || !module->SupportsKeybind()) {
                    continue;
                }

                const int key = module->GetKeybind();
                const auto currentIt = currentKeyStates.find(key);
                if (currentIt == currentKeyStates.end() || !currentIt->second) {
                    continue;
                }

                const auto previousIt = m_KeybindDownStates.find(key);
                const bool wasDown = previousIt != m_KeybindDownStates.end() && previousIt->second;
                if (!wasDown) {
                    const bool wasEnabled = module->IsEnabled();
                    module->Toggle();
                    NotifyEnabledChangeIfNeeded(*module, wasEnabled, module->IsEnabled());
                }
            }
        }

        m_KeybindDownStates = currentKeyStates;
    }

    void SyncAllToConfig(void* configPtr) {
        if (!configPtr) {
            return;
        }

        ForEachModule([this, configPtr](Module& module) {
            module.SyncToConfig(configPtr);
            SyncModuleKeybindToConfig(module, configPtr);
        });
    }

    void SyncAllFromConfig(void* configPtr) {
        if (!configPtr) {
            return;
        }

        const bool primeOnly = !m_EnableStatePrimed;
        ForEachModule([this, configPtr](Module& module) {
            const bool wasEnabled = module.IsEnabled();
            module.SyncFromConfig(configPtr);
            SyncModuleKeybindFromConfig(module, configPtr);
            if (!m_EnableStatePrimed) {
                m_KnownEnabledStates[BuildModuleKey(module)] = module.IsEnabled();
                return;
            }

            NotifyEnabledChangeIfNeeded(module, wasEnabled, module.IsEnabled());
        });

        if (primeOnly) {
            m_EnableStatePrimed = true;
        }
    }

    void TickAll() {
        ForEachModule([](Module& module) {
            if (!module.IsSynchronous()) {
                module.Tick();
            }
        });
    }

    void TickSynchronousAll(void* env) {
        ForEachModule([env](Module& module) {
            if (module.IsSynchronous()) {
                module.TickSynchronous(env);
            }
        });
    }

    void RenderOverlayAll(ImDrawList* drawList, float screenW, float screenH) {
        ForEachModule([drawList, screenW, screenH](Module& module) {
            module.RenderOverlay(drawList, screenW, screenH);
        });
    }

    void ShutdownRuntimeAll(void* env) {
        ForEachModule([env](Module& module) {
            module.ShutdownRuntime(env);
        });
    }

    void UpdateLauncher(void* configPtr = nullptr) {
        ProcessKeybinds();
        if (configPtr) {
            SyncAllToConfig(configPtr);
        }
    }

    void UpdateRuntime(void* configPtr = nullptr) {
        if (configPtr) {
            SyncAllFromConfig(configPtr);
        }
        TickAll();
    }

    void Update(void* configPtr = nullptr) {
        ProcessKeybinds();
        if (configPtr) {
            SyncAllToConfig(configPtr);
        }
        TickAll();
    }

    void SyncModuleKeybindToConfig(Module& module, void* configPtr) {
        if (!configPtr || !module.SupportsKeybind()) {
            return;
        }

        auto* config = static_cast<ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        ModuleConfig::KeybindEntry* entry = FindOrCreateKeybindEntry(*config, module);
        if (!entry) {
            return;
        }

        entry->m_Category = static_cast<int>(module.GetCategory());
        std::strncpy(entry->m_ModuleName, module.GetName().c_str(), sizeof(entry->m_ModuleName) - 1);
        entry->m_ModuleName[sizeof(entry->m_ModuleName) - 1] = '\0';
        entry->m_Keybind = module.GetKeybind();
    }

    void SyncModuleKeybindFromConfig(Module& module, void* configPtr) {
        if (!configPtr || !module.SupportsKeybind()) {
            return;
        }

        const auto* config = static_cast<const ModuleConfig*>(configPtr);
        if (!config) {
            return;
        }

        const ModuleConfig::KeybindEntry* entry = FindKeybindEntry(*config, module);
        if (!entry) {
            return;
        }

        module.SetKeybind(entry->m_Keybind);
    }

private:
    ModuleManager() = default;

    template <typename Callback>
    void ForEachModule(Callback&& callback) {
        for (auto& [category, modules] : m_Modules) {
            (void)category;
            for (auto& module : modules) {
                if (module) {
                    callback(*module);
                }
            }
        }
    }

    static std::string BuildModuleKey(const Module& module) {
        return std::to_string(static_cast<int>(module.GetCategory())) + ":" + module.GetName();
    }

    static bool IsKeybindEntryForModule(const ModuleConfig::KeybindEntry& entry, const Module& module) {
        return entry.m_Category == static_cast<int>(module.GetCategory()) &&
               std::strcmp(entry.m_ModuleName, module.GetName().c_str()) == 0;
    }

    static const ModuleConfig::KeybindEntry* FindKeybindEntry(const ModuleConfig& config, const Module& module) {
        const int count = (config.Keybinds.m_Count < 0) ? 0 : config.Keybinds.m_Count;
        const int maxCount = static_cast<int>(sizeof(config.Keybinds.m_Entries) / sizeof(config.Keybinds.m_Entries[0]));
        const int safeCount = (count > maxCount) ? maxCount : count;
        for (int index = 0; index < safeCount; ++index) {
            const auto& entry = config.Keybinds.m_Entries[index];
            if (IsKeybindEntryForModule(entry, module)) {
                return &entry;
            }
        }

        return nullptr;
    }

    static ModuleConfig::KeybindEntry* FindOrCreateKeybindEntry(ModuleConfig& config, const Module& module) {
        const int maxCount = static_cast<int>(sizeof(config.Keybinds.m_Entries) / sizeof(config.Keybinds.m_Entries[0]));
        const int count = (config.Keybinds.m_Count < 0) ? 0 : ((config.Keybinds.m_Count > maxCount) ? maxCount : config.Keybinds.m_Count);
        for (int index = 0; index < count; ++index) {
            auto& entry = config.Keybinds.m_Entries[index];
            if (IsKeybindEntryForModule(entry, module)) {
                return &entry;
            }
        }

        if (count >= maxCount) {
            return nullptr;
        }

        config.Keybinds.m_Count = count + 1;
        auto& entry = config.Keybinds.m_Entries[count];
        entry = ModuleConfig::KeybindEntry{};
        entry.m_Category = static_cast<int>(module.GetCategory());
        std::strncpy(entry.m_ModuleName, module.GetName().c_str(), sizeof(entry.m_ModuleName) - 1);
        entry.m_ModuleName[sizeof(entry.m_ModuleName) - 1] = '\0';
        return &entry;
    }

    void NotifyEnabledChangeIfNeeded(const Module& module, bool wasEnabled, bool isEnabled) {
        const std::string key = BuildModuleKey(module);
        auto stateIt = m_KnownEnabledStates.find(key);
        if (stateIt == m_KnownEnabledStates.end()) {
            m_KnownEnabledStates.emplace(key, isEnabled);
            return;
        }

        if (wasEnabled == isEnabled && stateIt->second == isEnabled) {
            return;
        }

        stateIt->second = isEnabled;
        if (m_ModuleToggleCallback) {
            m_ModuleToggleCallback(module, isEnabled);
        }
    }

    std::unordered_map<int, bool> CaptureBoundKeyStates() const {
        std::unordered_map<int, bool> states;
        for (const auto& [category, modules] : m_Modules) {
            (void)category;
            for (const auto& module : modules) {
                if (!module || !module->SupportsKeybind()) {
                    continue;
                }

                const int key = module->GetKeybind();
                if (key <= 0 || key >= 256 || states.find(key) != states.end()) {
                    continue;
                }

                states.emplace(key, (GetAsyncKeyState(key) & 0x8000) != 0);
            }
        }

        return states;
    }

    bool IsKeybindInputBlocked() const {
        return m_KeybindInputBlockPredicate && m_KeybindInputBlockPredicate();
    }

    bool IsKeybindInputAllowed() const {
        HWND foregroundWindow = GetForegroundWindow();
        if (!foregroundWindow || !IsWindow(foregroundWindow)) {
            return false;
        }

        DWORD foregroundProcessId = 0;
        GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
        if (foregroundProcessId == 0) {
            return false;
        }

        if (foregroundProcessId == GetCurrentProcessId()) {
            return true;
        }

        if (m_KeybindProcessPredicate) {
            return m_KeybindProcessPredicate(foregroundProcessId);
        }

        return false;
    }

    std::unordered_map<ModuleCategory, std::vector<std::shared_ptr<Module>>> m_Modules;
    std::unordered_map<std::string, bool> m_KnownEnabledStates;
    std::unordered_map<int, bool> m_KeybindDownStates;
    KeybindProcessPredicate m_KeybindProcessPredicate;
    KeybindInputBlockPredicate m_KeybindInputBlockPredicate;
    ModuleToggleCallback m_ModuleToggleCallback;
    bool m_EnableStatePrimed = false;
};
