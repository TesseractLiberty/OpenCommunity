#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <cstring>

namespace MemoryScrub {
    inline void Wipe(volatile void* ptr, size_t size) {
        if (!ptr || size == 0) return;
        volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
        for (size_t i = 0; i < size; ++i) p[i] = 0;
    }
    template <typename T>
    inline void Wipe(volatile T* ptr, size_t count) {
        if (ptr && count > 0) Wipe(static_cast<volatile void*>(ptr), count * sizeof(T));
    }
    template <typename T, size_t N>
    inline void WipeArray(T (&arr)[N]) { Wipe(arr, N); }
    template <size_t N>
    inline void WipeCharArray(char (&arr)[N]) { Wipe(arr, N); }

    inline void ClearString(std::string& s) {
        // IMPORTANTE: resize(capacity) antes de Wipe — MSVC clear() só zera [0]
        const size_t capacity = s.capacity();
        if (capacity > 0) { s.resize(capacity); Wipe(s.data(), s.size()); }
        s.clear(); s.shrink_to_fit();
    }
    inline void ClearStringVector(std::vector<std::string>& vec) {
        for (auto& str : vec) ClearString(str); vec.clear();
    }
    inline void ClearWString(std::wstring& s) {
        const size_t capacity = s.capacity();
        if (capacity > 0) {
            s.resize(capacity, L'\0');
            volatile wchar_t* p = s.data();
            for (size_t i = 0; i < s.size(); ++i) p[i] = L'\0';
        }
        s.clear(); s.shrink_to_fit();
    }
    inline void WipeStack(void* ptr, size_t size) {
        if (!ptr || size == 0) return;
        volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
        for (size_t i = 0; i < size; ++i) p[i] = 0;
    }
    template <size_t N> inline void WipeStackArray(char (&arr)[N]) { WipeStack(arr, N); }
    template <size_t N> inline void WipeStackArray(wchar_t (&arr)[N]) { WipeStack(arr, N * sizeof(wchar_t)); }
}
