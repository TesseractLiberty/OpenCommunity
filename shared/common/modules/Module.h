#pragma once

#include "ModuleOption.h"

#include <atomic>
#include <chrono>
#include <string>
#include <vector>
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

class Module {
public:
    Module(const std::string& name, const std::string& description, ModuleCategory category)
        : m_Name(name), m_Description(description), m_Category(category) {}
    virtual ~Module() = default;

    const std::string& GetName() const { return m_Name; }
    const std::string& GetDescription() const { return m_Description; }
    ModuleCategory GetCategory() const { return m_Category; }
    bool IsEnabled() const { return m_Enabled; }
    bool IsBeta() const { return m_IsBeta; }

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
        if (m_Keybind == 0) {
            return "...";
        }

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

    virtual void OnOptionEdited(size_t optionIndex) {
        (void)optionIndex;
    }

    virtual void SyncToConfig(void* configPtr) {
        (void)configPtr;
    }

    virtual void SyncFromConfig(void* configPtr) {
        (void)configPtr;
    }

    virtual void Tick() {}

    virtual bool IsSynchronous() const { return false; }

    virtual void TickSynchronous(void* env) {
        (void)env;
    }

    virtual void RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
        (void)drawList;
        (void)screenW;
        (void)screenH;
    }

    virtual void ShutdownRuntime(void* env) {
        (void)env;
    }

    virtual std::string GetTag() const { return ""; }

    bool IsInUse() const {
        return m_Enabled && GetUsageNowMs() <= m_InUseUntilMs.load(std::memory_order_relaxed);
    }

    const unsigned char* GetImageData() const { return m_ImageData; }
    unsigned int GetImageSize() const { return m_ImageSize; }
    const std::string& GetImagePath() const { return m_ImagePath; }

protected:
    void AddOption(const ModuleOption& option) {
        m_Options.push_back(option);
    }

    void SetImagePrefix(const unsigned char* data, unsigned int size) {
        m_ImageData = data;
        m_ImageSize = size;
    }

    void SetImagePath(const std::string& path) {
        m_ImagePath = path;
    }

    void SetBeta(bool isBeta = true) {
        m_IsBeta = isBeta;
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
    std::string m_ImagePath;
    bool m_IsBeta = false;

private:
    static long long GetUsageNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::atomic<long long> m_InUseUntilMs{ 0 };
};
