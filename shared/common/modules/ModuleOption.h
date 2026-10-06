#pragma once
#include <string>
#include <vector>
#include <initializer_list>
#include "../security/MemoryScrub.h"
#include "../security/XorString.h"
using string_obfuscation::EncString;

enum class OptionType { Toggle, SliderInt, SliderFloat, Combo, Color, Text, Button };

struct ModuleOption {
    EncString name;
    OptionType type = OptionType::Toggle;
    bool boolValue = false;
    int intValue = 0;
    float floatValue = 0.0f;
    float colorValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int intMin = 0, intMax = 100;
    float floatMin = 0.0f, floatMax = 1.0f;
    std::vector<EncString> comboItems;
    int comboIndex = 0;
    std::string textValue;
    int textMaxLength = 127;
    bool interactive = true;
    bool buttonPressed = false;
    EncString buttonLabel;
    int displayOrder = -1;
    bool showPlayerHead = false;
    EncString playerHeadName;
    bool statusOnly = false;

    void Clear() {
        name.Wipe();
        for (auto& it : comboItems) it.Wipe();
        comboItems.clear(); comboItems.shrink_to_fit();
        MemoryScrub::ClearString(textValue);
        buttonLabel.Wipe(); playerHeadName.Wipe();
        comboIndex = 0; displayOrder = -1; showPlayerHead = false; statusOnly = false;
    }
    const char* name_c_str() const { return name.c_str(); }

    static ModuleOption Toggle(const char* n, bool v = false) {
        ModuleOption o; o.name.EncryptFrom(n ? n : ""); o.type = OptionType::Toggle; o.boolValue = v; return o;
    }
    static ModuleOption ToggleReadOnly(const char* n, bool v = false) {
        ModuleOption o = Toggle(n, v); o.interactive = false; return o;
    }
    static ModuleOption SliderInt(const char* n, int def, int mn, int mx) {
        ModuleOption o; o.name.EncryptFrom(n ? n : ""); o.type = OptionType::SliderInt;
        o.intValue = def; o.intMin = mn; o.intMax = mx; return o;
    }
    static ModuleOption SliderFloat(const char* n, float def, float mn, float mx) {
        ModuleOption o; o.name.EncryptFrom(n ? n : ""); o.type = OptionType::SliderFloat;
        o.floatValue = def; o.floatMin = mn; o.floatMax = mx; return o;
    }
    static ModuleOption Combo(const char* n, std::initializer_list<const char*> items, int def = 0) {
        ModuleOption o; o.name.EncryptFrom(n ? n : ""); o.type = OptionType::Combo;
        o.comboItems.reserve(items.size());
        for (auto* it : items) o.comboItems.emplace_back(it ? it : "");
        o.comboIndex = def; return o;
    }
    static ModuleOption Color(const char* n, float r, float g, float b, float a = 1.0f) {
        ModuleOption o; o.name.EncryptFrom(n ? n : ""); o.type = OptionType::Color;
        o.colorValue[0]=r; o.colorValue[1]=g; o.colorValue[2]=b; o.colorValue[3]=a; return o;
    }
    static ModuleOption Text(const char* n, const std::string& def = {}, int maxLen = 127) {
        ModuleOption o; o.name.EncryptFrom(n ? n : ""); o.type = OptionType::Text;
        o.textValue = def; o.textMaxLength = maxLen < 1 ? 1 : maxLen; return o;
    }
    static ModuleOption Button(const char* n, const std::string& label = {}) {
        ModuleOption o; o.name.EncryptFrom(n ? n : ""); o.type = OptionType::Button;
        o.buttonLabel.EncryptFrom(label.empty() ? (n ? n : "") : label.c_str()); return o;
    }
};
