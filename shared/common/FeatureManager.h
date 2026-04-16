#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <windows.h>

struct ImDrawList;

#define MODULE_INFO(className, moduleName, moduleDescription, moduleCategory) \
    className() : Module(moduleName, moduleDescription, moduleCategory)

enum class ModuleCategory {
    Combat,
    Movement,
    Visuals,
    Settings
};

enum class OptionType {
    Toggle,
    SliderInt,
    SliderFloat,
    Combo,
    Color
};

struct ModuleOption {
    std::string name;
    OptionType type;

    bool boolValue = false;
    int intValue = 0;
    float floatValue = 0.0f;
    float colorValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    int intMin = 0, intMax = 100;
    float floatMin = 0.0f, floatMax = 1.0f;

    std::vector<std::string> comboItems;
    int comboIndex = 0;

    static ModuleOption Toggle(const std::string& name, bool defaultValue = false) {
        ModuleOption o;
        o.name = name;
        o.type = OptionType::Toggle;
        o.boolValue = defaultValue;
        return o;
    }

    static ModuleOption SliderInt(const std::string& name, int defaultValue, int min, int max) {
        ModuleOption o;
        o.name = name;
        o.type = OptionType::SliderInt;
        o.intValue = defaultValue;
        o.intMin = min;
        o.intMax = max;
        return o;
    }

    static ModuleOption SliderFloat(const std::string& name, float defaultValue, float min, float max) {
        ModuleOption o;
        o.name = name;
        o.type = OptionType::SliderFloat;
        o.floatValue = defaultValue;
        o.floatMin = min;
        o.floatMax = max;
        return o;
    }

    static ModuleOption Combo(const std::string& name, const std::vector<std::string>& items, int defaultIndex = 0) {
        ModuleOption o;
        o.name = name;
        o.type = OptionType::Combo;
        o.comboItems = items;
        o.comboIndex = defaultIndex;
        return o;
    }

    static ModuleOption Color(const std::string& name, float r, float g, float b, float a = 1.0f) {
        ModuleOption o;
        o.name = name;
        o.type = OptionType::Color;
        o.colorValue[0] = r;
        o.colorValue[1] = g;
        o.colorValue[2] = b;
        o.colorValue[3] = a;
        return o;
    }
};

class Module {
public:
    Module(const std::string& name, const std::string& description, ModuleCategory category)
        : m_Name(name), m_Description(description), m_Category(category) {}
    virtual ~Module() = default;

    const std::string& GetName() const { return m_Name; }
    const std::string& GetDescription() const { return m_Description; }
    ModuleCategory GetCategory() const { return m_Category; }
    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) {
        m_Enabled = enabled;
        if (!enabled) {
            ClearInUse();
        }
    }
    void Toggle() { SetEnabled(!m_Enabled); }

    int GetKeybind() const { return m_Keybind; }
    void SetKeybind(int key) { m_Keybind = key; }
    virtual bool SupportsKeybind() const { return true; }
    std::string GetKeybindName() const {
        if (m_Keybind == 0) return "...";
        char name[32] = {};
        UINT scanCode = MapVirtualKeyA(m_Keybind, MAPVK_VK_TO_VSC);
        GetKeyNameTextA(scanCode << 16, name, sizeof(name));
        return name[0] ? name : "...";
    }

    std::vector<ModuleOption>& GetOptions() { return m_Options; }
    const std::vector<ModuleOption>& GetOptions() const { return m_Options; }
    virtual bool ShouldRenderOption(size_t optionIndex) const {
        (void)optionIndex;
        return true;
    }

    virtual void SyncToConfig(void* configPtr) { (void)configPtr; }
    virtual void SyncFromConfig(void* configPtr) { (void)configPtr; }
    virtual void Tick() {}

    virtual bool IsSynchronous() const { return false; }
    virtual void TickSynchronous(void* env) { (void)env; }
    virtual void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
        (void)drawList;
        (void)screenW;
        (void)screenH;
    }

    // Tag displayed in the ArrayList HUD (e.g. "15-25cps")
    virtual std::string GetTag() const { return ""; }
    bool IsInUse() const {
        return m_Enabled && GetUsageNowMs() <= m_InUseUntilMs.load(std::memory_order_relaxed);
    }

    const unsigned char* GetImageData() const { return m_ImageData; }
    unsigned int GetImageSize() const { return m_ImageSize; }

protected:
    void AddOption(const ModuleOption& option) {
        m_Options.push_back(option);
    }

    void SetImagePrefix(const unsigned char* data, unsigned int size) {
        m_ImageData = data;
        m_ImageSize = size;
    }

    void MarkInUse(int holdMs = 150) {
        const long long durationMs = (holdMs < 0) ? 0 : holdMs;
        const long long target = GetUsageNowMs() + durationMs;

        long long current = m_InUseUntilMs.load(std::memory_order_relaxed);
        while (current < target &&
               !m_InUseUntilMs.compare_exchange_weak(current, target, std::memory_order_relaxed)) {
        }
    }

    void ClearInUse() {
        m_InUseUntilMs.store(0, std::memory_order_relaxed);
    }

    std::string m_Name;
    std::string m_Description;
    ModuleCategory m_Category;
    bool m_Enabled = false;
    int m_Keybind = 0;
    std::vector<ModuleOption> m_Options;
    const unsigned char* m_ImageData = nullptr;
    unsigned int m_ImageSize = 0;

private:
    static long long GetUsageNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::atomic<long long> m_InUseUntilMs{ 0 };
};

class FeatureManager {
public:
    static FeatureManager* Get() {
        static FeatureManager instance;
        return &instance;
    }

    void RegisterModule(std::shared_ptr<Module> module) {
        m_Modules[module->GetCategory()].push_back(module);
    }

    const std::vector<std::shared_ptr<Module>>& GetModules(ModuleCategory category) const {
        static const std::vector<std::shared_ptr<Module>> empty;
        auto it = m_Modules.find(category);
        if (it != m_Modules.end()) return it->second;
        return empty;
    }

    std::vector<std::shared_ptr<Module>> GetAllModules() const {
        std::vector<std::shared_ptr<Module>> all;
        for (auto& [cat, mods] : m_Modules) {
            for (auto& m : mods) all.push_back(m);
        }
        return all;
    }

    void ProcessKeybinds() {
        for (auto& [cat, mods] : m_Modules) {
            for (auto& mod : mods) {
                if (!mod->SupportsKeybind()) continue;
                int vk = mod->GetKeybind();
                if (vk == 0) continue;
                if (GetAsyncKeyState(vk) & 1) {
                    mod->Toggle();
                }
            }
        }
    }

    void SyncAllToConfig(void* configPtr) {
        if (!configPtr) return;
        for (auto& [cat, mods] : m_Modules) {
            for (auto& mod : mods) {
                mod->SyncToConfig(configPtr);
            }
        }
    }

    void SyncAllFromConfig(void* configPtr) {
        if (!configPtr) return;
        for (auto& [cat, mods] : m_Modules) {
            for (auto& mod : mods) {
                mod->SyncFromConfig(configPtr);
            }
        }
    }

    void TickAll() {
        for (auto& [cat, mods] : m_Modules) {
            for (auto& mod : mods) {
                if (!mod->IsSynchronous())
                    mod->Tick();
            }
        }
    }

    void TickSynchronousAll(void* env) {
        for (auto& [cat, mods] : m_Modules) {
            for (auto& mod : mods) {
                if (mod->IsSynchronous())
                    mod->TickSynchronous(env);
            }
        }
    }

    void RenderOverlayAll(ImDrawList* drawList, float screenW, float screenH) {
        for (auto& [cat, mods] : m_Modules) {
            for (auto& mod : mods) {
                mod->RenderOverlay(drawList, screenW, screenH);
            }
        }
    }

    void UpdateFrontdoor(void* configPtr = nullptr) {
        ProcessKeybinds();
        if (configPtr) SyncAllToConfig(configPtr);
    }

    void UpdateBackdoor(void* configPtr = nullptr) {
        if (configPtr) SyncAllFromConfig(configPtr);
        TickAll();
    }

    void Update(void* configPtr = nullptr) {
        ProcessKeybinds();
        if (configPtr) SyncAllToConfig(configPtr);
        TickAll();
    }

private:
    FeatureManager() = default;
    std::unordered_map<ModuleCategory, std::vector<std::shared_ptr<Module>>> m_Modules;
};
