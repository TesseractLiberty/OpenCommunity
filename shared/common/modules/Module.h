#pragma once

#include "ModuleOption.h"
#include "../security/MemoryScrub.h"
#include "../security/XorString.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

using string_obfuscation::EncString;
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
    Module(const char* name, const char* description, ModuleCategory category) {
        if (name) { m_Name.EncryptFrom(name); m_NameHash = HashName(name); }
        if (description) m_Description.EncryptFrom(description);
        m_Category = category;
    }
    virtual ~Module() = default;

    bool GetNameInto(char* out, std::size_t maxLen) const {
        if (!out || maxLen == 0) return false;
        return m_Name.DecryptInto(out, maxLen);
    }
    std::uint64_t GetNameHash() const { return m_NameHash; }
    const EncString& GetNameEnc() const { return m_Name; }
    bool GetDescriptionInto(char* out, std::size_t maxLen) const {
        if (!out || maxLen == 0) return false;
        return m_Description.DecryptInto(out, maxLen);
    }
    const EncString& GetDescriptionEnc() const { return m_Description; }
    ModuleCategory GetCategory() const { return m_Category; }
    virtual const char* GetIcon() const { return nullptr; }
    bool IsEnabled() const { return m_Enabled; }
    bool IsBeta() const { return m_IsBeta; }
    int GetId() const { return m_Id; }
    void SetId(int id) { m_Id = id; }
    void SetEnabled(bool enabled) {
        const bool was = m_Enabled; m_Enabled = enabled;
        if (!enabled) ClearInUse();
        if (was != enabled) OnEnabledChange(was, enabled);
    }
    virtual void OnEnabledChange(bool was, bool is) { (void)was; (void)is; }
    virtual void RenderSettings() {}
    void Toggle() { SetEnabled(!m_Enabled); }
    int GetKeybind() const { return m_Keybind; }
    void SetKeybind(int key) { m_Keybind = key; }
    virtual bool SupportsKeybind() const { return true; }

    const char* GetKeybindName() const {
        if (m_Keybind == 0) {
            char* s = string_obfuscation::GetThreadRing().Acquire(8);
            s[0]='N'; s[1]='O'; s[2]='N'; s[3]='E'; s[4]='\0'; return s;
        }
        char name[32] = {};
        UINT sc = MapVirtualKeyA(m_Keybind, MAPVK_VK_TO_VSC);
        GetKeyNameTextA(sc << 16, name, sizeof(name));
        if (!name[0]) {
            char* s = string_obfuscation::GetThreadRing().Acquire(8);
            s[0]='.'; s[1]='.'; s[2]='.'; s[3]='\0'; return s;
        }
        const std::size_t len = std::strlen(name) + 1;
        char* slot = string_obfuscation::GetThreadRing().Acquire(len);
        std::memcpy(slot, name, len); return slot;
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
    virtual std::string GetDisplayName() const { return {}; }

    void ScrubTextBuffers() {
        m_Name.Wipe(); m_Description.Wipe(); m_ImagePath.Wipe();
        for (auto& o : m_Options) o.Clear();
        m_Options.clear(); m_Options.shrink_to_fit();
    }
    void ClearOptions() { for (auto& o : m_Options) o.Clear(); }
    bool IsInUse() const {
        return m_Enabled && GetUsageNowMs() <= m_InUseUntilMs.load(std::memory_order_relaxed);
    }
    const unsigned char* GetImageData() const { return m_ImageData; }
    unsigned int GetImageSize() const { return m_ImageSize; }
    const EncString& GetImagePathEnc() const { return m_ImagePath; }
    bool GetImagePathInto(char* out, std::size_t maxLen) const {
        if (!out || maxLen == 0) return false; return m_ImagePath.DecryptInto(out, maxLen);
    }
    bool HasImagePath() const { return !m_ImagePath.IsEmpty(); }
protected:
    void AddOption(const ModuleOption& option) {
        m_Options.push_back(option);
    }
    void SetImagePrefix(const unsigned char* data, unsigned int size) {
        m_ImageData = data;
        m_ImageSize = size;
    }
    void SetImagePath(const char* path) { m_ImagePath.EncryptFrom(path); }
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

    EncString m_Name;
    EncString m_Description;
    ModuleCategory m_Category{};
    bool m_Enabled = false;
    int m_Id = -1;
    int m_Keybind = 0;
    std::vector<ModuleOption> m_Options;
    const unsigned char* m_ImageData = nullptr;
    unsigned int m_ImageSize = 0;
    EncString m_ImagePath;
    bool m_IsBeta = false;
    std::uint64_t m_NameHash = 0;
private:
    static long long GetUsageNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    std::atomic<long long> m_InUseUntilMs{ 0 };
    static std::uint64_t HashName(const char* name) {
        if (!name) return 0;
        std::uint64_t h = 1469598103934665603ull;
        for (const char* p = name; *p; ++p) { h ^= static_cast<unsigned char>(*p); h *= 1099511628211ull; }
        return h;
    }
};

class ScopedName {
public:
    explicit ScopedName(const Module& m) { m.GetNameInto(m_Buffer, sizeof(m_Buffer)); }
    ~ScopedName() { MemoryScrub::Wipe(m_Buffer, sizeof(m_Buffer)); }
    ScopedName(const ScopedName&) = delete;
    ScopedName& operator=(const ScopedName&) = delete;
    const char* c_str() const { return m_Buffer[0] ? m_Buffer : ""; }
    const char* Get() const { return m_Buffer[0] ? m_Buffer : ""; }
private:
    char m_Buffer[128]{};
};

class ScopedDesc {
public:
    explicit ScopedDesc(const Module& m) { m.GetDescriptionInto(m_Buffer, sizeof(m_Buffer)); }
    ~ScopedDesc() { MemoryScrub::Wipe(m_Buffer, sizeof(m_Buffer)); }
    ScopedDesc(const ScopedDesc&) = delete;
    ScopedDesc& operator=(const ScopedDesc&) = delete;
    const char* c_str() const { return m_Buffer[0] ? m_Buffer : ""; }
private:
    char m_Buffer[256]{};
};
