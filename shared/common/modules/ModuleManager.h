#pragma once

#include "Module.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

class ModuleManager {
public:
    using KeybindProcessPredicate = std::function<bool(DWORD)>;
    using ModuleToggleCallback = std::function<void(const Module& module, bool enabled)>;

    static ModuleManager* Get() {
        static ModuleManager instance;
        return &instance;
    }

    void SetKeybindProcessPredicate(KeybindProcessPredicate predicate) {
        m_KeybindProcessPredicate = std::move(predicate);
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
        if (!IsKeybindInputAllowed()) {
            return;
        }

        for (auto& [category, modules] : m_Modules) {
            (void)category;
            for (auto& module : modules) {
                if (!module || !module->SupportsKeybind()) {
                    continue;
                }

                const int key = module->GetKeybind();
                if (key != 0 && (GetAsyncKeyState(key) & 1)) {
                    const bool wasEnabled = module->IsEnabled();
                    module->Toggle();
                    NotifyEnabledChangeIfNeeded(*module, wasEnabled, module->IsEnabled());
                }
            }
        }
    }

    void SyncAllToConfig(void* configPtr) {
        if (!configPtr) {
            return;
        }

        ForEachModule([configPtr](Module& module) {
            module.SyncToConfig(configPtr);
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
    KeybindProcessPredicate m_KeybindProcessPredicate;
    ModuleToggleCallback m_ModuleToggleCallback;
    bool m_EnableStatePrimed = false;
};
