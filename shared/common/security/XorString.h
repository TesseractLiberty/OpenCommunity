#pragma once

#include "MemoryScrub.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <windows.h>

namespace string_obfuscation {
constexpr std::uint64_t Seed(const char* time, std::uint64_t line, std::uint64_t counter) {
    std::uint64_t value = 1469598103934665603ull ^ line ^ (counter << 32);
    for (std::size_t index = 0; time[index] != '\0'; ++index) {
        value = (value ^ static_cast<unsigned char>(time[index])) * 1099511628211ull;
    }
    return value;
}

constexpr std::uint64_t Mix(std::uint64_t value) {
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    return value * 2685821657736338717ull;
}

template <typename CharT>
constexpr CharT KeyAt(std::uint64_t key, std::size_t index) {
    using UnsignedCharT = std::make_unsigned_t<CharT>;
    std::uint64_t mixed = key;
    for (std::size_t step = 0; step <= index; ++step) {
        mixed = Mix(mixed + step + 0x9e3779b97f4a7c15ull);
    }
    return static_cast<CharT>(static_cast<UnsignedCharT>(mixed));
}

struct XorRingSlot { char buffer[256]; };
constexpr std::size_t kRingSlotCount = 32;
constexpr std::size_t kRingSlotSize  = sizeof(XorRingSlot::buffer);

class XorRingBuffer {
public:
    XorRingBuffer() { Register(); }
    ~XorRingBuffer() { Unregister(); }
    XorRingBuffer(const XorRingBuffer&) = delete;
    XorRingBuffer& operator=(const XorRingBuffer&) = delete;

    void Wipe() {
        for (std::size_t i = 0; i < kRingSlotCount; ++i) {
            volatile char* p = reinterpret_cast<volatile char*>(m_Slots[i].buffer);
            for (std::size_t j = 0; j < kRingSlotSize; ++j) p[j] = '\0';
        }
        m_Index.store(0, std::memory_order_relaxed);
    }
    char* Acquire(std::size_t needed) {
        const std::size_t idx = m_Index.fetch_add(1, std::memory_order_relaxed) & (kRingSlotCount - 1);
        char* slot = m_Slots[idx].buffer;
        volatile char* p = reinterpret_cast<volatile char*>(slot);
        for (std::size_t j = 0; j < kRingSlotSize; ++j) p[j] = '\0';
        (void)needed; return slot;
    }
    wchar_t* AcquireW(std::size_t wcharsNeeded) {
        static_assert(kRingSlotSize >= 128 * sizeof(wchar_t), "ring slot too small for wide literals");
        const std::size_t idx = m_Index.fetch_add(1, std::memory_order_relaxed) & (kRingSlotCount - 1);
        char* raw = m_Slots[idx].buffer;
        volatile char* p = reinterpret_cast<volatile char*>(raw);
        for (std::size_t j = 0; j < kRingSlotSize; ++j) p[j] = '\0';
        (void)wcharsNeeded; return reinterpret_cast<wchar_t*>(raw);
    }
    static void WipeAll() {
        std::lock_guard<std::mutex> lock(GetMutex());
        for (XorRingBuffer* ring : GetInstances()) if (ring) ring->Wipe();
    }
private:
    static std::mutex& GetMutex() { static std::mutex m; return m; }
    static std::vector<XorRingBuffer*>& GetInstances() { static std::vector<XorRingBuffer*> v; return v; }
    void Register() { std::lock_guard<std::mutex> lock(GetMutex()); GetInstances().push_back(this); }
    void Unregister() {
        std::lock_guard<std::mutex> lock(GetMutex());
        auto& v = GetInstances();
        for (auto it = v.begin(); it != v.end(); ++it) if (*it == this) { v.erase(it); return; }
    }
    XorRingSlot m_Slots[kRingSlotCount];
    std::atomic<std::size_t> m_Index{ 0 };
};

inline XorRingBuffer& GetThreadRing() {
    static DWORD s_tlsIdx = TLS_OUT_OF_INDEXES;
    if (s_tlsIdx == TLS_OUT_OF_INDEXES) {
        DWORD idx = ::TlsAlloc();
        DWORD prev = (DWORD)InterlockedCompareExchange((volatile LONG*)&s_tlsIdx, (LONG)idx, (LONG)TLS_OUT_OF_INDEXES);
        if (prev != TLS_OUT_OF_INDEXES) ::TlsFree(idx);
    }
    auto* ring = static_cast<XorRingBuffer*>(::TlsGetValue(s_tlsIdx));
    if (!ring) { ring = new XorRingBuffer(); ::TlsSetValue(s_tlsIdx, ring); }
    return *ring;
}

template <typename CharT, std::size_t Size, std::uint64_t Key>
class XorLiteral {
public:
    static_assert(Size <= kRingSlotSize, "XOR literal too large for ring slot (max 256 bytes incl. NUL)");
    constexpr explicit XorLiteral(const CharT (&literal)[Size]) : m_Data{} {
        for (std::size_t i = 0; i < Size; ++i) m_Data[i] = static_cast<CharT>(literal[i] ^ KeyAt<CharT>(Key, i));
    }
    const CharT* Get() const {
        char* slot = GetThreadRing().Acquire(Size);
        const volatile CharT* enc = m_Data.data();
        for (std::size_t i = 0; i < Size; ++i) slot[i] = static_cast<char>(enc[i] ^ KeyAt<CharT>(Key, i));
        return slot;
    }
    std::basic_string<CharT> Decrypt() const {
        const CharT* d = Get();
        std::basic_string<CharT> r(Size > 0 ? Size - 1 : 0, CharT{});
        for (std::size_t i = 0; i + 1 < Size; ++i) r[i] = d[i];
        return r;
    }
    constexpr const std::array<CharT, Size>& Data() const { return m_Data; }
private:
    std::array<CharT, Size> m_Data;
};

template <std::uint64_t Key, typename CharT, std::size_t Size>
constexpr auto Make(const CharT (&literal)[Size]) { return XorLiteral<CharT, Size, Key>(literal); }

template <std::size_t Size, std::uint64_t Key>
class XorLiteral<wchar_t, Size, Key> {
public:
    static_assert(Size <= 128, "XOR-W literal too large for ring slot (max 127 chars + NUL)");
    constexpr explicit XorLiteral(const wchar_t (&literal)[Size]) : m_Data{} {
        for (std::size_t i = 0; i < Size; ++i) m_Data[i] = static_cast<wchar_t>(literal[i] ^ KeyAt<wchar_t>(Key, i));
    }
    const wchar_t* Get() const {
        wchar_t* slot = GetThreadRing().AcquireW(Size);
        const volatile wchar_t* enc = m_Data.data();
        for (std::size_t i = 0; i < Size; ++i) slot[i] = static_cast<wchar_t>(enc[i] ^ KeyAt<wchar_t>(Key, i));
        return slot;
    }
    std::basic_string<wchar_t> Decrypt() const {
        const wchar_t* d = Get();
        std::basic_string<wchar_t> r(Size > 0 ? Size - 1 : 0, wchar_t{});
        for (std::size_t i = 0; i + 1 < Size; ++i) r[i] = d[i];
        return r;
    }
    constexpr const std::array<wchar_t, Size>& Data() const { return m_Data; }
private:
    std::array<wchar_t, Size> m_Data;
};

class EncString {
public:
    static constexpr std::size_t kMaxLen = 511;
    EncString() = default;
    explicit EncString(const char* str) { EncryptFrom(str); }
    EncString(const EncString& other) { if (!other.m_Len) return; EncryptDecrypted(other); }
    EncString& operator=(const EncString& other) {
        if (this != &other) { Wipe(); if (other.m_Len) EncryptDecrypted(other); } return *this;
    }
    EncString& operator=(const char* str) { EncryptFrom(str); return *this; }
    EncString(EncString&& other) noexcept : m_Data(other.m_Data), m_Len(other.m_Len), m_Key(other.m_Key) { other.Wipe(); }
    EncString& operator=(EncString&& other) noexcept {
        if (this != &other) { Wipe(); m_Data = other.m_Data; m_Len = other.m_Len; m_Key = other.m_Key; other.Wipe(); } return *this;
    }
    ~EncString() { Wipe(); }

    void EncryptFrom(const char* str) {
        Wipe(); if (!str) return;
        const std::size_t srcLen = std::strlen(str);
        const std::size_t copyLen = srcLen > kMaxLen ? kMaxLen : srcLen;
        m_Key = MakeRuntimeKey();
        const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(str);
        for (std::size_t i = 0; i < copyLen; ++i) m_Data[i] = static_cast<std::uint8_t>(src[i] ^ KeyAt<char>(m_Key, i));
        m_Data[copyLen] = static_cast<std::uint8_t>('\0' ^ KeyAt<char>(m_Key, copyLen));
        m_Len = static_cast<std::uint32_t>(copyLen);
        for (std::size_t i = copyLen + 1; i < m_Data.size(); ++i) m_Data[i] = 0;
    }
    bool DecryptInto(char* out, std::size_t maxLen) const {
        if (!out || maxLen == 0) return false;
        if (m_Len == 0) { out[0] = '\0'; return false; }
        const std::size_t copyLen = (static_cast<std::size_t>(m_Len) + 1 < maxLen) ? static_cast<std::size_t>(m_Len) + 1 : maxLen;
        for (std::size_t i = 0; i < copyLen; ++i) out[i] = static_cast<char>(m_Data[i] ^ KeyAt<char>(m_Key, i));
        out[copyLen - 1] = '\0'; return true;
    }
    const char* c_str() const {
        if (m_Len == 0) return "";
        char* slot = GetThreadRing().Acquire(static_cast<std::size_t>(m_Len) + 1);
        const std::size_t copyLen = static_cast<std::size_t>(m_Len) + 1;
        for (std::size_t i = 0; i < copyLen; ++i) slot[i] = static_cast<char>(m_Data[i] ^ KeyAt<char>(m_Key, i));
        return slot;
    }
    std::size_t Length() const { return m_Len; }
    bool IsEmpty() const { return m_Len == 0; }
    bool empty() const { return m_Len == 0; }
    void Wipe() {
        volatile std::uint8_t* p = m_Data.data();
        for (std::size_t i = 0; i < m_Data.size(); ++i) p[i] = 0;
        m_Len = 0; m_Key = 0;
    }
private:
    static std::uint64_t MakeRuntimeKey() {
        static std::atomic<std::uint64_t> counter{ 0 };
        const std::uint64_t c = counter.fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t addr = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&counter));
        const std::uint64_t tick = static_cast<std::uint64_t>(::GetTickCount64());
        std::uint64_t k = tick ^ addr ^ (c * 0x9e3779b97f4a7c15ull);
        if (k == 0) k = 0x1234567890abcdefull; return k;
    }
    void EncryptDecrypted(const EncString& other) {
        char tmp[kMaxLen + 1]; other.DecryptInto(tmp, sizeof(tmp));
        EncryptFrom(tmp); MemoryScrub::Wipe(tmp, sizeof(tmp));
    }
    std::array<std::uint8_t, kMaxLen + 1> m_Data{};
    std::uint32_t m_Len = 0;
    std::uint64_t m_Key = 0;
};

inline void WipeAllXorStrings() { XorRingBuffer::WipeAll(); }
}

#if defined(__cpp_constinit)
#define XOR_STRING_CONSTINIT constinit
#else
#define XOR_STRING_CONSTINIT
#endif

#define XOR_STRING_IMPL(literal, counter)                                                        \
    ([]() -> const char* {                                                                       \
        static XOR_STRING_CONSTINIT auto encrypted = ::string_obfuscation::Make<                 \
            ::string_obfuscation::Seed(__TIME__, static_cast<std::uint64_t>(counter),            \
                                       static_cast<std::uint64_t>(counter))>(literal);           \
        return encrypted.Get();                                                                  \
    }())

#define XOR_STRING(literal) XOR_STRING_IMPL(literal, __COUNTER__)
#define XOR(literal) XOR_STRING(literal)

#define XOR_W_IMPL(literal, counter)                                                         \
    ([]() -> const wchar_t* {                                                                \
        static XOR_STRING_CONSTINIT auto encrypted = ::string_obfuscation::Make<              \
            ::string_obfuscation::Seed(__TIME__, static_cast<std::uint64_t>(counter),         \
                                       static_cast<std::uint64_t>(counter))>(literal);        \
        return encrypted.Get();                                                               \
    }())

#define XOR_W(literal) XOR_W_IMPL(literal, __COUNTER__)
