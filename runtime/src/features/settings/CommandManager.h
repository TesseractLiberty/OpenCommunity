#pragma once

#include "../../../../shared/common/modules/ModuleManager.h"
#include "../../core/Bridge.h"
#include "CommandOutput.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace CommandManager {
    namespace detail {
        struct ModuleEntry {
            Module* module = nullptr;
            std::string commandName;
            std::string normalizedName;
            std::string acronym;
        };

        struct OptionEntry {
            ModuleOption* option = nullptr;
            size_t index = 0;
            std::string commandName;
            std::string normalizedName;
        };

        struct OptionMatch {
            OptionEntry option;
            size_t consumedTokens = 0;
        };

        inline std::string Trim(const std::string& value) {
            size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
                ++start;
            }

            size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
                --end;
            }

            return value.substr(start, end - start);
        }

        inline std::string ToLower(std::string value) {
            for (char& character : value) {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            return value;
        }

        inline std::string NormalizeKey(const std::string& value) {
            std::string normalized;
            normalized.reserve(value.size());
            for (char character : value) {
                const unsigned char ascii = static_cast<unsigned char>(character);
                if (std::isalnum(ascii) != 0) {
                    normalized.push_back(static_cast<char>(std::tolower(ascii)));
                }
            }
            return normalized;
        }

        inline std::string BuildOptionCommandName(const std::string& value) {
            std::string commandName;
            commandName.reserve(value.size() + 4);

            bool lastWasSeparator = true;
            char previousAlphaNumeric = '\0';
            for (char character : value) {
                const unsigned char ascii = static_cast<unsigned char>(character);
                if (std::isalnum(ascii) == 0) {
                    if (!commandName.empty() && !lastWasSeparator) {
                        commandName.push_back('-');
                        lastWasSeparator = true;
                    }
                    previousAlphaNumeric = '\0';
                    continue;
                }

                if (std::isupper(ascii) != 0 &&
                    previousAlphaNumeric != '\0' &&
                    std::islower(static_cast<unsigned char>(previousAlphaNumeric)) != 0 &&
                    !lastWasSeparator) {
                    commandName.push_back('-');
                    lastWasSeparator = true;
                }

                commandName.push_back(static_cast<char>(std::tolower(ascii)));
                lastWasSeparator = false;
                previousAlphaNumeric = static_cast<char>(ascii);
            }

            while (!commandName.empty() && commandName.back() == '-') {
                commandName.pop_back();
            }

            return commandName;
        }

        inline std::string BuildModuleCommandName(const std::string& value) {
            return NormalizeKey(value);
        }

        inline std::string BuildAcronym(const std::string& value) {
            std::string acronym;
            acronym.reserve(value.size());

            bool takeNext = true;
            char previousAlphaNumeric = '\0';
            for (char character : value) {
                const unsigned char ascii = static_cast<unsigned char>(character);
                if (std::isalnum(ascii) == 0) {
                    takeNext = true;
                    previousAlphaNumeric = '\0';
                    continue;
                }

                const bool isUpperBoundary =
                    std::isupper(ascii) != 0 &&
                    previousAlphaNumeric != '\0' &&
                    std::islower(static_cast<unsigned char>(previousAlphaNumeric)) != 0;

                if (takeNext || isUpperBoundary) {
                    acronym.push_back(static_cast<char>(std::tolower(ascii)));
                }

                takeNext = false;
                previousAlphaNumeric = static_cast<char>(ascii);
            }

            return acronym;
        }

        inline std::string JoinTokens(const std::vector<std::string>& tokens, size_t startIndex, size_t endIndexExclusive) {
            if (startIndex >= endIndexExclusive || startIndex >= tokens.size()) {
                return {};
            }

            std::string joined = tokens[startIndex];
            for (size_t index = startIndex + 1; index < endIndexExclusive && index < tokens.size(); ++index) {
                joined += ' ';
                joined += tokens[index];
            }

            return joined;
        }

        inline std::vector<std::string> Tokenize(const std::string& value) {
            std::vector<std::string> tokens;
            std::istringstream stream(value);
            std::string token;
            while (stream >> token) {
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        inline bool ParseInteger(const std::string& value, int& outValue) {
            char* end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            if (!end || *end != '\0') {
                return false;
            }

            outValue = static_cast<int>(parsed);
            return true;
        }

        inline bool ParseFloat(const std::string& value, float& outValue) {
            char* end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (!end || *end != '\0') {
                return false;
            }

            outValue = parsed;
            return true;
        }

        inline bool ParseBoolean(const std::string& value, bool& outValue) {
            const std::string normalized = NormalizeKey(value);
            if (normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes" || normalized == "enable" || normalized == "enabled") {
                outValue = true;
                return true;
            }

            if (normalized == "0" || normalized == "false" || normalized == "off" || normalized == "no" || normalized == "disable" || normalized == "disabled") {
                outValue = false;
                return true;
            }

            return false;
        }

        inline std::string FormatFloat(float value) {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%.2f", value);
            std::string text(buffer);

            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
            if (text.empty()) {
                text = "0";
            }
            return text;
        }

        inline std::string GetConfiguredPrefix(const ModuleConfig* config) {
            if (!config || config->GameChat.m_Prefix[0] == '\0') {
                return ".";
            }
            return config->GameChat.m_Prefix;
        }

        inline std::vector<ModuleEntry> GetModuleEntries() {
            std::vector<ModuleEntry> entries;
            auto modules = ModuleManager::Get()->GetAllModules();
            entries.reserve(modules.size());
            for (const auto& module : modules) {
                if (!module) {
                    continue;
                }

                ModuleEntry entry;
                entry.module = module.get();
                entry.commandName = BuildModuleCommandName(module->GetName());
                entry.normalizedName = NormalizeKey(module->GetName());
                entry.acronym = BuildAcronym(module->GetName());
                entries.push_back(std::move(entry));
            }

            std::sort(entries.begin(), entries.end(), [](const ModuleEntry& left, const ModuleEntry& right) {
                return ToLower(left.module->GetName()) < ToLower(right.module->GetName());
            });
            return entries;
        }

        inline std::optional<ModuleEntry> FindModuleExact(const std::string& token) {
            const std::string normalizedToken = NormalizeKey(token);
            if (normalizedToken.empty()) {
                return std::nullopt;
            }

            for (const auto& entry : GetModuleEntries()) {
                if (entry.normalizedName == normalizedToken || entry.commandName == normalizedToken || (!entry.acronym.empty() && entry.acronym == normalizedToken)) {
                    return entry;
                }
            }

            return std::nullopt;
        }

        inline std::vector<ModuleEntry> FindModuleMatches(const std::string& token) {
            const std::string normalizedToken = NormalizeKey(token);
            std::vector<ModuleEntry> matches;
            for (const auto& entry : GetModuleEntries()) {
                if (normalizedToken.empty() ||
                    entry.normalizedName.rfind(normalizedToken, 0) == 0 ||
                    entry.commandName.rfind(normalizedToken, 0) == 0 ||
                    (!entry.acronym.empty() && entry.acronym.rfind(normalizedToken, 0) == 0)) {
                    matches.push_back(entry);
                }
            }

            return matches;
        }

        inline std::vector<OptionEntry> GetOptionEntries(Module& module) {
            std::vector<OptionEntry> entries;
            auto& options = module.GetOptions();
            entries.reserve(options.size());

            for (size_t index = 0; index < options.size(); ++index) {
                ModuleOption& option = options[index];
                if (!option.interactive || option.statusOnly || !module.ShouldRenderOption(index)) {
                    continue;
                }

                OptionEntry entry;
                entry.option = &option;
                entry.index = index;
                entry.commandName = BuildOptionCommandName(option.name);
                entry.normalizedName = NormalizeKey(option.name);
                entries.push_back(std::move(entry));
            }

            return entries;
        }

        inline std::optional<OptionMatch> FindOptionExact(Module& module, const std::vector<std::string>& tokens, size_t startIndex) {
            if (startIndex >= tokens.size()) {
                return std::nullopt;
            }

            const auto optionEntries = GetOptionEntries(module);
            std::optional<OptionMatch> bestMatch;
            for (const auto& optionEntry : optionEntries) {
                for (size_t endIndex = tokens.size(); endIndex > startIndex; --endIndex) {
                    const std::string normalizedTokens = NormalizeKey(JoinTokens(tokens, startIndex, endIndex));
                    if (normalizedTokens.empty()) {
                        continue;
                    }

                    if (normalizedTokens == optionEntry.normalizedName || normalizedTokens == NormalizeKey(optionEntry.commandName)) {
                        OptionMatch match;
                        match.option = optionEntry;
                        match.consumedTokens = endIndex - startIndex;
                        if (!bestMatch || match.consumedTokens > bestMatch->consumedTokens) {
                            bestMatch = match;
                        }
                        break;
                    }
                }
            }

            return bestMatch;
        }

        inline std::vector<OptionEntry> FindOptionMatches(Module& module, const std::string& fragment) {
            const std::string normalizedFragment = NormalizeKey(fragment);
            std::vector<OptionEntry> matches;
            for (const auto& optionEntry : GetOptionEntries(module)) {
                if (normalizedFragment.empty() ||
                    optionEntry.normalizedName.rfind(normalizedFragment, 0) == 0 ||
                    NormalizeKey(optionEntry.commandName).rfind(normalizedFragment, 0) == 0) {
                    matches.push_back(optionEntry);
                }
            }
            return matches;
        }

        inline std::string FormatOptionValue(const ModuleOption& option) {
            switch (option.type) {
            case OptionType::Toggle:
                return option.boolValue ? "on" : "off";
            case OptionType::SliderInt:
                return std::to_string(option.intValue);
            case OptionType::SliderFloat:
                return FormatFloat(option.floatValue);
            case OptionType::Combo:
                if (option.comboIndex >= 0 && option.comboIndex < static_cast<int>(option.comboItems.size())) {
                    return option.comboItems[option.comboIndex];
                }
                return "unknown";
            case OptionType::Color: {
                const int red = static_cast<int>(std::clamp(option.colorValue[0], 0.0f, 1.0f) * 255.0f);
                const int green = static_cast<int>(std::clamp(option.colorValue[1], 0.0f, 1.0f) * 255.0f);
                const int blue = static_cast<int>(std::clamp(option.colorValue[2], 0.0f, 1.0f) * 255.0f);
                char buffer[48] = {};
                std::snprintf(buffer, sizeof(buffer), "%d,%d,%d", red, green, blue);
                return buffer;
            }
            case OptionType::Text:
                return option.textValue.empty() ? "\"\"" : option.textValue;
            case OptionType::Button:
                return "button";
            default:
                return {};
            }
        }

        inline std::string FormatOptionUsage(const OptionEntry& entry) {
            if (!entry.option) {
                return {};
            }

            switch (entry.option->type) {
            case OptionType::Toggle:
                return entry.commandName + " <on/off>";
            case OptionType::SliderInt:
                return entry.commandName + " <" + std::to_string(entry.option->intMin) + "-" + std::to_string(entry.option->intMax) + ">";
            case OptionType::SliderFloat:
                return entry.commandName + " <" + FormatFloat(entry.option->floatMin) + "-" + FormatFloat(entry.option->floatMax) + ">";
            case OptionType::Combo: {
                std::string usage = entry.commandName + " <";
                const size_t maxPreviewItems = 4;
                for (size_t index = 0; index < entry.option->comboItems.size() && index < maxPreviewItems; ++index) {
                    if (index > 0) {
                        usage += "/";
                    }
                    usage += BuildOptionCommandName(entry.option->comboItems[index]);
                }
                if (entry.option->comboItems.size() > maxPreviewItems) {
                    usage += "/...";
                }
                usage += ">";
                return usage;
            }
            case OptionType::Color:
                return entry.commandName + " <r g b>";
            case OptionType::Text:
                return entry.commandName + " <text>";
            case OptionType::Button:
                return entry.commandName;
            default:
                return entry.commandName;
            }
        }

        inline std::string BuildModuleSummary(Module& module) {
            std::string summary = module.IsEnabled() ? "enabled" : "disabled";

            const auto optionEntries = GetOptionEntries(module);
            const size_t maxVisibleOptions = 4;
            size_t shownOptions = 0;
            for (const auto& entry : optionEntries) {
                if (!entry.option) {
                    continue;
                }

                summary += " | ";
                summary += entry.commandName;
                summary += "=";
                summary += FormatOptionValue(*entry.option);

                ++shownOptions;
                if (shownOptions >= maxVisibleOptions) {
                    break;
                }
            }

            if (optionEntries.size() > shownOptions) {
                summary += " | +";
                summary += std::to_string(optionEntries.size() - shownOptions);
                summary += " more";
            }

            return summary;
        }

        inline std::string BuildModulesList() {
            std::string list;
            bool first = true;
            for (const auto& entry : GetModuleEntries()) {
                if (!entry.module) {
                    continue;
                }

                if (!first) {
                    list += ", ";
                }

                list += entry.commandName;
                first = false;
            }
            return list;
        }

        inline void CommitModule(Module& module, ModuleConfig* config) {
            if (!config) {
                return;
            }

            module.SyncToConfig(config);
            module.SyncFromConfig(config);
        }

        inline void SendRootHelp(JNIEnv* env, const std::string& prefix) {
            CommandOutput::SendInfo(
                env,
                "Use " + prefix + "t <module> to toggle modules or " + prefix + "<module> <option>. Try " + prefix + "modules and press Tab in chat for autocomplete.",
                "Commands");
        }

        inline void SendModulesList(JNIEnv* env) {
            CommandOutput::SendInfo(env, BuildModulesList(), "Modules");
        }

        inline void SendModuleOverview(JNIEnv* env, Module& module, const std::string& prefix) {
            CommandOutput::SendInfo(
                env,
                BuildModuleSummary(module) + ". Use " + prefix + "t " + BuildModuleCommandName(module.GetName()) + " to toggle or " + prefix + BuildModuleCommandName(module.GetName()) + " help for commands.",
                module.GetName());
        }

        inline void SendModuleHelp(JNIEnv* env, Module& module) {
            std::string message = "Commands: status, help";
            const auto optionEntries = GetOptionEntries(module);
            const size_t maxPreviewOptions = 6;
            size_t shownOptions = 0;
            for (const auto& entry : optionEntries) {
                if (!entry.option) {
                    continue;
                }

                message += " | ";
                message += FormatOptionUsage(entry);
                ++shownOptions;
                if (shownOptions >= maxPreviewOptions) {
                    break;
                }
            }

            if (optionEntries.size() > shownOptions) {
                message += " | +";
                message += std::to_string(optionEntries.size() - shownOptions);
                message += " more";
            }

            CommandOutput::SendInfo(env, message, module.GetName());
        }

        inline bool HandleModuleToggle(JNIEnv* env, Module& module, ModuleConfig* config, bool enabled) {
            module.SetEnabled(enabled);
            CommitModule(module, config);
            CommandOutput::SendSuccess(
                env,
                module.GetName() + std::string(enabled ? " enabled." : " disabled."),
                module.GetName());
            return true;
        }

        inline std::optional<int> FindComboIndex(const ModuleOption& option, const std::string& rawValue) {
            const std::string normalizedValue = NormalizeKey(rawValue);
            if (normalizedValue.empty()) {
                return std::nullopt;
            }

            std::optional<int> prefixMatch;
            int prefixMatchCount = 0;
            for (int index = 0; index < static_cast<int>(option.comboItems.size()); ++index) {
                const std::string normalizedItem = NormalizeKey(option.comboItems[index]);
                const std::string normalizedCommandItem = NormalizeKey(BuildOptionCommandName(option.comboItems[index]));
                if (normalizedValue == normalizedItem || normalizedValue == normalizedCommandItem) {
                    return index;
                }

                if (normalizedItem.rfind(normalizedValue, 0) == 0 || normalizedCommandItem.rfind(normalizedValue, 0) == 0) {
                    prefixMatch = index;
                    ++prefixMatchCount;
                }
            }

            if (prefixMatchCount == 1) {
                return prefixMatch;
            }

            return std::nullopt;
        }

        inline bool HandleOptionCommand(
            JNIEnv* env,
            Module& module,
            ModuleConfig* config,
            const OptionMatch& match,
            const std::vector<std::string>& valueTokens,
            const std::string& commandPrefix) {
            if (!match.option.option) {
                CommandOutput::SendError(env, "Unknown option.", module.GetName());
                return true;
            }

            ModuleOption& option = *match.option.option;
            switch (option.type) {
            case OptionType::Toggle: {
                bool nextValue = !option.boolValue;
                if (!valueTokens.empty() && !ParseBoolean(valueTokens.front(), nextValue)) {
                    CommandOutput::SendError(env, "Use on/off, true/false or 1/0 for " + match.option.commandName + ".", module.GetName());
                    return true;
                }

                option.boolValue = nextValue;
                module.OnOptionEdited(match.option.index);
                CommitModule(module, config);
                CommandOutput::SendSuccess(env, match.option.commandName + " set to " + std::string(nextValue ? "on" : "off") + ".", module.GetName());
                return true;
            }
            case OptionType::SliderInt: {
                if (valueTokens.empty()) {
                    CommandOutput::SendError(
                        env,
                        "Use " + commandPrefix + " " + match.option.commandName + " <" + std::to_string(option.intMin) + "-" + std::to_string(option.intMax) + ">.",
                        module.GetName());
                    return true;
                }

                int parsedValue = 0;
                if (!ParseInteger(valueTokens.front(), parsedValue)) {
                    CommandOutput::SendError(env, match.option.commandName + " only accepts whole numbers.", module.GetName());
                    return true;
                }

                option.intValue = (std::clamp)(parsedValue, option.intMin, option.intMax);
                module.OnOptionEdited(match.option.index);
                CommitModule(module, config);
                CommandOutput::SendSuccess(env, match.option.commandName + " set to " + std::to_string(option.intValue) + ".", module.GetName());
                return true;
            }
            case OptionType::SliderFloat: {
                if (valueTokens.empty()) {
                    CommandOutput::SendError(
                        env,
                        "Use " + commandPrefix + " " + match.option.commandName + " <" + FormatFloat(option.floatMin) + "-" + FormatFloat(option.floatMax) + ">.",
                        module.GetName());
                    return true;
                }

                float parsedValue = 0.0f;
                if (!ParseFloat(valueTokens.front(), parsedValue)) {
                    CommandOutput::SendError(env, match.option.commandName + " only accepts numbers.", module.GetName());
                    return true;
                }

                option.floatValue = (std::clamp)(parsedValue, option.floatMin, option.floatMax);
                module.OnOptionEdited(match.option.index);
                CommitModule(module, config);
                CommandOutput::SendSuccess(env, match.option.commandName + " set to " + FormatFloat(option.floatValue) + ".", module.GetName());
                return true;
            }
            case OptionType::Combo: {
                if (valueTokens.empty()) {
                    CommandOutput::SendError(env, "Choose a value for " + match.option.commandName + " or press Tab for suggestions.", module.GetName());
                    return true;
                }

                const auto comboIndex = FindComboIndex(option, JoinTokens(valueTokens, 0, valueTokens.size()));
                if (!comboIndex.has_value()) {
                    CommandOutput::SendError(env, "Unknown value for " + match.option.commandName + ".", module.GetName());
                    return true;
                }

                option.comboIndex = *comboIndex;
                module.OnOptionEdited(match.option.index);
                CommitModule(module, config);
                const std::string currentValue = option.comboItems[option.comboIndex];
                CommandOutput::SendSuccess(env, match.option.commandName + " set to " + currentValue + ".", module.GetName());
                return true;
            }
            case OptionType::Color: {
                if (valueTokens.size() < 3) {
                    CommandOutput::SendError(env, "Use " + commandPrefix + " " + match.option.commandName + " <r g b>.", module.GetName());
                    return true;
                }

                int red = 0;
                int green = 0;
                int blue = 0;
                if (!ParseInteger(valueTokens[0], red) || !ParseInteger(valueTokens[1], green) || !ParseInteger(valueTokens[2], blue)) {
                    CommandOutput::SendError(env, match.option.commandName + " expects RGB values between 0 and 255.", module.GetName());
                    return true;
                }

                option.colorValue[0] = (std::clamp)(red, 0, 255) / 255.0f;
                option.colorValue[1] = (std::clamp)(green, 0, 255) / 255.0f;
                option.colorValue[2] = (std::clamp)(blue, 0, 255) / 255.0f;
                module.OnOptionEdited(match.option.index);
                CommitModule(module, config);
                CommandOutput::SendSuccess(env, match.option.commandName + " updated.", module.GetName());
                return true;
            }
            case OptionType::Text: {
                const std::string nextValue = JoinTokens(valueTokens, 0, valueTokens.size());
                option.textValue = nextValue;
                module.OnOptionEdited(match.option.index);
                CommitModule(module, config);
                CommandOutput::SendSuccess(env, match.option.commandName + " updated.", module.GetName());
                return true;
            }
            case OptionType::Button: {
                if (!valueTokens.empty()) {
                    const std::string action = NormalizeKey(valueTokens.front());
                    if (action != "run" && action != "click" && action != "open" && action != "clear" && action != "use" && action != "trigger") {
                        CommandOutput::SendError(env, "Use " + commandPrefix + " " + match.option.commandName + " or add run.", module.GetName());
                        return true;
                    }
                }

                option.buttonPressed = true;
                module.OnOptionEdited(match.option.index);
                CommitModule(module, config);
                CommandOutput::SendSuccess(env, match.option.commandName + " triggered.", module.GetName());
                return true;
            }
            default:
                CommandOutput::SendError(env, "Unsupported option type.", module.GetName());
                return true;
            }
        }

        inline bool HandleModuleCommand(
            JNIEnv* env,
            Module& module,
            ModuleConfig* config,
            const std::string& prefix,
            const std::vector<std::string>& tokens) {
            const std::string moduleCommandName = BuildModuleCommandName(module.GetName());
            const std::string commandPrefix = prefix + moduleCommandName;
            if (tokens.size() <= 1) {
                SendModuleOverview(env, module, prefix);
                return true;
            }

            const std::string action = NormalizeKey(tokens[1]);
            if (action == "status") {
                SendModuleOverview(env, module, prefix);
                return true;
            }

            if (action == "help" || action == "options" || action == "list") {
                SendModuleHelp(env, module);
                return true;
            }

            size_t optionStartIndex = 1;
            if (action == "set") {
                optionStartIndex = 2;
            }

            const auto match = FindOptionExact(module, tokens, optionStartIndex);
            if (!match.has_value()) {
                CommandOutput::SendError(env, "Unknown command or option.", module.GetName());
                SendModuleHelp(env, module);
                return true;
            }

            const size_t valueStart = optionStartIndex + match->consumedTokens;
            std::vector<std::string> valueTokens;
            if (valueStart < tokens.size()) {
                valueTokens.assign(tokens.begin() + valueStart, tokens.end());
            }
            return HandleOptionCommand(env, module, config, *match, valueTokens, commandPrefix);
        }

        inline bool HandleToggleCommand(
            JNIEnv* env,
            ModuleConfig* config,
            const std::string& prefix,
            const std::vector<std::string>& tokens) {
            if (tokens.size() <= 1) {
                CommandOutput::SendInfo(env, "Use " + prefix + "t <module>.", "Commands");
                return true;
            }

            const auto moduleEntry = FindModuleExact(tokens[1]);
            if (!moduleEntry.has_value() || !moduleEntry->module) {
                CommandOutput::SendError(env, "Unknown module for toggle. Use " + prefix + "modules to list them.", "Commands");
                return true;
            }

            return HandleModuleToggle(env, *moduleEntry->module, config, !moduleEntry->module->IsEnabled());
        }

        inline std::vector<std::string> BuildRootSuggestions(const std::string& prefix, const std::string& fragment) {
            const std::string normalizedFragment = NormalizeKey(fragment);
            std::vector<std::string> suggestions;

            const std::vector<std::string> rootCommands = {
                "t",
                "help",
                "modules",
                "commands"
            };

            for (const std::string& command : rootCommands) {
                if (normalizedFragment.empty() || NormalizeKey(command).rfind(normalizedFragment, 0) == 0) {
                    suggestions.push_back(prefix + command);
                }
            }

            for (const auto& entry : FindModuleMatches(fragment)) {
                suggestions.push_back(prefix + entry.commandName);
            }

            return suggestions;
        }

        inline std::vector<std::string> BuildModuleSuggestions(const std::string& prefix, Module& module, const std::string& fragment) {
            const std::string normalizedFragment = NormalizeKey(fragment);
            const std::string base = prefix + BuildModuleCommandName(module.GetName()) + " ";
            std::vector<std::string> suggestions;

            const std::vector<std::string> builtinCommands = {
                "status",
                "help"
            };

            for (const std::string& command : builtinCommands) {
                if (normalizedFragment.empty() || NormalizeKey(command).rfind(normalizedFragment, 0) == 0) {
                    suggestions.push_back(base + command);
                }
            }

            for (const auto& optionEntry : FindOptionMatches(module, fragment)) {
                suggestions.push_back(base + optionEntry.commandName);
            }

            return suggestions;
        }

        inline std::vector<std::string> BuildToggleSuggestions(const std::string& prefix, const std::string& fragment) {
            std::vector<std::string> suggestions;
            const std::string base = prefix + "t ";
            for (const auto& entry : FindModuleMatches(fragment)) {
                suggestions.push_back(base + entry.commandName);
            }
            return suggestions;
        }

        inline std::vector<std::string> BuildOptionValueSuggestions(
            const std::string& commandBase,
            const OptionEntry& optionEntry,
            const std::string& fragment) {
            std::vector<std::string> suggestions;
            if (!optionEntry.option) {
                return suggestions;
            }

            const std::string normalizedFragment = NormalizeKey(fragment);
            const std::string base = commandBase + optionEntry.commandName + " ";
            switch (optionEntry.option->type) {
            case OptionType::Toggle: {
                const std::vector<std::string> toggleValues = { "on", "off" };
                for (const std::string& value : toggleValues) {
                    if (normalizedFragment.empty() || NormalizeKey(value).rfind(normalizedFragment, 0) == 0) {
                        suggestions.push_back(base + value);
                    }
                }
                break;
            }
            case OptionType::Combo:
                for (const std::string& item : optionEntry.option->comboItems) {
                    const std::string itemCommand = BuildOptionCommandName(item);
                    if (normalizedFragment.empty() || NormalizeKey(itemCommand).rfind(normalizedFragment, 0) == 0 || NormalizeKey(item).rfind(normalizedFragment, 0) == 0) {
                        suggestions.push_back(base + itemCommand);
                    }
                }
                break;
            case OptionType::Button:
                if (normalizedFragment.empty() || NormalizeKey("run").rfind(normalizedFragment, 0) == 0) {
                    suggestions.push_back(base + "run");
                }
                break;
            default:
                break;
            }

            return suggestions;
        }
    }

    inline bool TryHandleText(JNIEnv* env, const std::string& text) {
        auto* bridge = Bridge::Get();
        ModuleConfig* config = bridge ? bridge->GetConfig() : nullptr;
        if (!config || !config->GameChat.m_UseGameChat) {
            return false;
        }

        const std::string prefix = detail::GetConfiguredPrefix(config);
        if (text.rfind(prefix, 0) != 0) {
            return false;
        }

        const std::string payload = detail::Trim(text.substr(prefix.size()));
        const auto tokens = detail::Tokenize(payload);
        if (tokens.empty()) {
            detail::SendRootHelp(env, prefix);
            return true;
        }

        const std::string rootToken = detail::NormalizeKey(tokens[0]);
        if (rootToken == "help" || rootToken == "commands") {
            if (tokens.size() > 1) {
                if (const auto moduleEntry = detail::FindModuleExact(tokens[1]); moduleEntry.has_value() && moduleEntry->module) {
                    detail::SendModuleHelp(env, *moduleEntry->module);
                    return true;
                }
            }

            detail::SendRootHelp(env, prefix);
            return true;
        }

        if (rootToken == "modules") {
            detail::SendModulesList(env);
            return true;
        }

        if (rootToken == "t") {
            return detail::HandleToggleCommand(env, config, prefix, tokens);
        }

        const auto moduleEntry = detail::FindModuleExact(tokens[0]);
        if (!moduleEntry.has_value() || !moduleEntry->module) {
            CommandOutput::SendError(env, "Unknown module. Use " + prefix + "modules to list the available ones.", "Commands");
            return true;
        }

        return detail::HandleModuleCommand(env, *moduleEntry->module, config, prefix, tokens);
    }

    inline std::vector<std::string> CollectAutocompleteMatches(const std::string& text) {
        auto* bridge = Bridge::Get();
        ModuleConfig* config = bridge ? bridge->GetConfig() : nullptr;
        if (!config || !config->GameChat.m_UseGameChat) {
            return {};
        }

        const std::string prefix = detail::GetConfiguredPrefix(config);
        if (text.rfind(prefix, 0) != 0) {
            return {};
        }

        const std::string payload = text.substr(prefix.size());
        const bool endsWithSpace = !payload.empty() && std::isspace(static_cast<unsigned char>(payload.back())) != 0;
        const auto tokens = detail::Tokenize(payload);
        if (tokens.empty()) {
            return detail::BuildRootSuggestions(prefix, {});
        }

        if (detail::NormalizeKey(tokens[0]) == "t") {
            if (tokens.size() == 1 && !endsWithSpace) {
                return detail::BuildToggleSuggestions(prefix, {});
            }

            if (tokens.size() == 1 && endsWithSpace) {
                return detail::BuildToggleSuggestions(prefix, {});
            }

            if (tokens.size() == 2) {
                const std::string fragment = endsWithSpace ? std::string() : tokens[1];
                return detail::BuildToggleSuggestions(prefix, fragment);
            }

            return {};
        }

        if (tokens.size() == 1 && !endsWithSpace) {
            if (const auto moduleEntry = detail::FindModuleExact(tokens[0]); moduleEntry.has_value() && moduleEntry->module) {
                return detail::BuildModuleSuggestions(prefix, *moduleEntry->module, {});
            }
            return detail::BuildRootSuggestions(prefix, tokens[0]);
        }

        const auto moduleEntry = detail::FindModuleExact(tokens[0]);
        if (!moduleEntry.has_value() || !moduleEntry->module) {
            return {};
        }

        Module& module = *moduleEntry->module;
        if (tokens.size() == 1 && endsWithSpace) {
            return detail::BuildModuleSuggestions(prefix, module, {});
        }

        if (tokens.size() == 2 && !endsWithSpace) {
            if (const auto optionMatch = detail::FindOptionExact(module, tokens, 1); optionMatch.has_value() && optionMatch->consumedTokens == 1) {
                return detail::BuildOptionValueSuggestions(prefix + detail::BuildModuleCommandName(module.GetName()) + " ", optionMatch->option, {});
            }
            return detail::BuildModuleSuggestions(prefix, module, tokens[1]);
        }

        if (detail::NormalizeKey(tokens[1]) == "set") {
            if (tokens.size() == 2 || (tokens.size() == 3 && !endsWithSpace)) {
                const std::string fragment = tokens.size() >= 3 ? tokens[2] : std::string();
                std::vector<std::string> suggestions;
                const std::string base = prefix + detail::BuildModuleCommandName(module.GetName()) + " set ";
                for (const auto& optionEntry : detail::FindOptionMatches(module, fragment)) {
                    suggestions.push_back(base + optionEntry.commandName);
                }
                return suggestions;
            }

            if (const auto optionMatch = detail::FindOptionExact(module, tokens, 2); optionMatch.has_value() && optionMatch->consumedTokens == 1) {
                const std::string fragment = endsWithSpace ? std::string() : tokens.back();
                return detail::BuildOptionValueSuggestions(prefix + detail::BuildModuleCommandName(module.GetName()) + " set ", optionMatch->option, fragment);
            }
            return {};
        }

        if (const auto optionMatch = detail::FindOptionExact(module, tokens, 1); optionMatch.has_value()) {
            if (optionMatch->consumedTokens == 1) {
                const bool awaitingValue = endsWithSpace || tokens.size() > 2;
                const std::string valueFragment = awaitingValue && !endsWithSpace && tokens.size() > 2 ? tokens.back() : std::string();
                return detail::BuildOptionValueSuggestions(prefix + detail::BuildModuleCommandName(module.GetName()) + " ", optionMatch->option, valueFragment);
            }
        }

        return {};
    }
}
