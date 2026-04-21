#pragma once

#include <string>
#include <vector>

enum class OptionType {
    Toggle,
    SliderInt,
    SliderFloat,
    Combo,
    Color,
    Text,
    Button
};

struct ModuleOption {
    std::string name;
    OptionType type;

    bool boolValue = false;
    int intValue = 0;
    float floatValue = 0.0f;
    float colorValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    int intMin = 0;
    int intMax = 100;
    float floatMin = 0.0f;
    float floatMax = 1.0f;

    std::vector<std::string> comboItems;
    int comboIndex = 0;
    std::string textValue;
    int textMaxLength = 127;
    bool interactive = true;
    bool buttonPressed = false;
    std::string buttonLabel;
    int displayOrder = -1;
    bool showPlayerHead = false;
    std::string playerHeadName;
    bool statusOnly = false;

    static ModuleOption Toggle(const std::string& name, bool defaultValue = false) {
        ModuleOption option;
        option.name = name;
        option.type = OptionType::Toggle;
        option.boolValue = defaultValue;
        return option;
    }

    static ModuleOption ToggleReadOnly(const std::string& name, bool value = false) {
        ModuleOption option = Toggle(name, value);
        option.interactive = false;
        return option;
    }

    static ModuleOption SliderInt(const std::string& name, int defaultValue, int min, int max) {
        ModuleOption option;
        option.name = name;
        option.type = OptionType::SliderInt;
        option.intValue = defaultValue;
        option.intMin = min;
        option.intMax = max;
        return option;
    }

    static ModuleOption SliderFloat(const std::string& name, float defaultValue, float min, float max) {
        ModuleOption option;
        option.name = name;
        option.type = OptionType::SliderFloat;
        option.floatValue = defaultValue;
        option.floatMin = min;
        option.floatMax = max;
        return option;
    }

    static ModuleOption Combo(const std::string& name, const std::vector<std::string>& items, int defaultIndex = 0) {
        ModuleOption option;
        option.name = name;
        option.type = OptionType::Combo;
        option.comboItems = items;
        option.comboIndex = defaultIndex;
        return option;
    }

    static ModuleOption Color(const std::string& name, float red, float green, float blue, float alpha = 1.0f) {
        ModuleOption option;
        option.name = name;
        option.type = OptionType::Color;
        option.colorValue[0] = red;
        option.colorValue[1] = green;
        option.colorValue[2] = blue;
        option.colorValue[3] = alpha;
        return option;
    }

    static ModuleOption Text(const std::string& name, const std::string& defaultValue = {}, int maxLength = 127) {
        ModuleOption option;
        option.name = name;
        option.type = OptionType::Text;
        option.textValue = defaultValue;
        option.textMaxLength = (maxLength < 1) ? 1 : maxLength;
        return option;
    }

    static ModuleOption Button(const std::string& name, const std::string& label = {}) {
        ModuleOption option;
        option.name = name;
        option.type = OptionType::Button;
        option.buttonLabel = label.empty() ? name : label;
        return option;
    }
};
