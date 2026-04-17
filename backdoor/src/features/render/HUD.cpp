#include "pch.h"
#include "HUD.h"
#include "../../core/Bridge.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/jni/Class.h"
#include "../../game/jni/Field.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/jni/Method.h"
#include "../../game/mapping/Mapper.h"
#include "../../../../shared/common/FeatureManager.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/colors.h"
#include "../../../../deps/imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace {
    ImVec2 CalcTextSize(ImFont* font, float fontSize, const std::string& text) {
        if (!font) {
            return ImGui::CalcTextSize(text.c_str());
        }

        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    }

    std::string ReadString(JNIEnv* env, jstring value) {
        if (!env || !value) {
            return {};
        }

        const char* chars = env->GetStringUTFChars(value, nullptr);
        if (!chars) {
            return {};
        }

        std::string result(chars);
        env->ReleaseStringUTFChars(value, chars);
        return result;
    }

    JNIEnv* GetCurrentEnv() {
        return g_Game ? g_Game->GetCurrentEnv() : nullptr;
    }

    bool ShouldRenderIngameHud() {
        if (!g_Game || !g_Game->IsInitialized()) {
            return false;
        }

        JNIEnv* env = GetCurrentEnv();
        if (!env) {
            return false;
        }

        jobject world = Minecraft::GetTheWorld(env);
        if (!world) {
            return false;
        }

        jobject player = Minecraft::GetThePlayer(env);
        if (!player) {
            env->DeleteLocalRef(world);
            return false;
        }

        env->DeleteLocalRef(player);
        env->DeleteLocalRef(world);
        return true;
    }

    ImU32 MakeColorU32(float r, float g, float b, float a = 1.0f) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
    }

    void DrawShadowedText(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& pos, ImU32 color, ImU32 shadowColor, const std::string& text) {
        drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadowColor, text.c_str());
        drawList->AddText(font, fontSize, pos, color, text.c_str());
    }

    ImU32 McColorToImU32(char code) {
        switch (static_cast<char>(std::tolower(static_cast<unsigned char>(code)))) {
        case '0': return IM_COL32(0, 0, 0, 255);
        case '1': return IM_COL32(0, 0, 170, 255);
        case '2': return IM_COL32(0, 170, 0, 255);
        case '3': return IM_COL32(0, 170, 170, 255);
        case '4': return IM_COL32(170, 0, 0, 255);
        case '5': return IM_COL32(170, 0, 170, 255);
        case '6': return IM_COL32(255, 170, 0, 255);
        case '7': return IM_COL32(170, 170, 170, 255);
        case '8': return IM_COL32(85, 85, 85, 255);
        case '9': return IM_COL32(85, 85, 255, 255);
        case 'a': return IM_COL32(85, 255, 85, 255);
        case 'b': return IM_COL32(85, 255, 255, 255);
        case 'c': return IM_COL32(255, 85, 85, 255);
        case 'd': return IM_COL32(255, 85, 255, 255);
        case 'e': return IM_COL32(255, 255, 85, 255);
        case 'f': return IM_COL32(255, 255, 255, 255);
        case 'r': return IM_COL32(255, 255, 255, 255);
        default: return IM_COL32(255, 255, 255, 255);
        }
    }

    bool TryConsumeMinecraftColorCode(const std::string& text, size_t index, char& outCode, size_t& consumed) {
        if (index + 2 < text.size() &&
            static_cast<unsigned char>(text[index]) == 0xC2 &&
            static_cast<unsigned char>(text[index + 1]) == 0xA7) {
            outCode = text[index + 2];
            consumed = 3;
            return true;
        }

        if (index + 1 < text.size() && static_cast<unsigned char>(text[index]) == 0xA7) {
            outCode = text[index + 1];
            consumed = 2;
            return true;
        }

        return false;
    }

    std::string FormatModuleName(const std::string& name, bool spacedModules) {
        if (!spacedModules || name.empty()) {
            return name;
        }

        std::string result;
        result.reserve(name.size() + 4);

        for (size_t i = 0; i < name.size(); ++i) {
            const unsigned char current = static_cast<unsigned char>(name[i]);
            if (i > 0 && std::isupper(current)) {
                const unsigned char previous = static_cast<unsigned char>(name[i - 1]);
                const bool followsLowercase = std::islower(previous) != 0;
                const bool breaksAcronym = std::isupper(previous) != 0 &&
                    (i + 1) < name.size() &&
                    std::islower(static_cast<unsigned char>(name[i + 1])) != 0;
                if (followsLowercase || breaksAcronym) {
                    result.push_back(' ');
                }
            }

            result.push_back(static_cast<char>(current));
        }

        return result;
    }

    std::string GetConfigNick(const ModuleConfig* config) {
        if (!config || config->m_Username[0] == '\0') {
            return {};
        }

        return config->m_Username;
    }

    std::string ResolvePlayerNick(ModuleConfig* config) {
        static std::string cachedNick;
        static auto lastAttempt = std::chrono::steady_clock::time_point{};

        const auto now = std::chrono::steady_clock::now();
        if (!cachedNick.empty() && now - lastAttempt < std::chrono::milliseconds(500)) {
            return cachedNick;
        }

        lastAttempt = now;

        if (!g_Game || !g_Game->IsInitialized()) {
            cachedNick = GetConfigNick(config);
            return cachedNick;
        }

        JNIEnv* env = GetCurrentEnv();
        if (!env) {
            cachedNick = GetConfigNick(config);
            return cachedNick;
        }

        const auto mcClassName = Mapper::Get("net/minecraft/client/Minecraft");
        const auto mcClassDesc = Mapper::Get("net/minecraft/client/Minecraft", 2);
        const auto playerClassName = Mapper::Get("net/minecraft/client/entity/EntityPlayerSP");
        const auto playerClassDesc = Mapper::Get("net/minecraft/client/entity/EntityPlayerSP", 2);
        const auto theMinecraftName = Mapper::Get("theMinecraft");
        const auto thePlayerName = Mapper::Get("thePlayer");
        const auto getNameName = Mapper::Get("getName");

        if (mcClassName.empty() || mcClassDesc.empty() || playerClassName.empty() ||
            playerClassDesc.empty() || theMinecraftName.empty() || thePlayerName.empty() ||
            getNameName.empty()) {
            cachedNick = GetConfigNick(config);
            return cachedNick;
        }

        auto* mcClass = g_Game->FindClass(mcClassName);
        auto* playerClass = g_Game->FindClass(playerClassName);
        if (!mcClass || !playerClass) {
            cachedNick = GetConfigNick(config);
            return cachedNick;
        }

        auto* theMinecraftField = mcClass->GetField(env, theMinecraftName.c_str(), mcClassDesc.c_str(), true);
        auto* thePlayerField = mcClass->GetField(env, thePlayerName.c_str(), playerClassDesc.c_str());
        auto* getNameMethod = playerClass->GetMethod(env, getNameName.c_str(), "()Ljava/lang/String;");
        if (!theMinecraftField || !thePlayerField || !getNameMethod) {
            cachedNick = GetConfigNick(config);
            return cachedNick;
        }

        jobject mcInstance = theMinecraftField->GetObjectField(env, mcClass, true);
        if (!mcInstance) {
            cachedNick = GetConfigNick(config);
            return cachedNick;
        }

        jobject player = thePlayerField->GetObjectField(env, mcInstance);
        env->DeleteLocalRef(mcInstance);
        if (!player) {
            cachedNick = GetConfigNick(config);
            return cachedNick;
        }

        jstring playerName = static_cast<jstring>(getNameMethod->CallObjectMethod(env, player));
        cachedNick = ReadString(env, playerName);

        if (playerName) {
            env->DeleteLocalRef(playerName);
        }
        env->DeleteLocalRef(player);

        if (!cachedNick.empty() && config) {
            strncpy_s(config->m_Username, sizeof(config->m_Username), cachedNick.c_str(), _TRUNCATE);
        } else if (cachedNick.empty()) {
            cachedNick = GetConfigNick(config);
        }

        return cachedNick;
    }
}

void HUD::SetFonts(ImFont* regular, ImFont* bold, ImFont* vape) {
    m_RegularFont = regular;
    m_BoldFont = bold;
    m_VapeFont = vape;
}

void HUD::GetRainbowRGB(int offset, float& r, float& g, float& b) {
    const auto now = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const ImVec4 themed = color::GetCycleVec4((float)((ms + offset) % 10000) / 10000.0f);
    r = themed.x;
    g = themed.y;
    b = themed.z;
}

void HUD::GetRiseRGB(int offset, float& r, float& g, float& b) {
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now.time_since_epoch()).count();
    const float phase = 0.5f + 0.5f * sinf(seconds * 1.35f + offset * 0.5f);
    const float hue = 0.58f - phase * 0.18f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.72f, 0.95f, r, g, b);
}

void HUD::GetVapeV4RGB(int offset, float& r, float& g, float& b) {
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now.time_since_epoch()).count();
    const float phase = 0.5f + 0.5f * sinf(seconds * 1.55f + offset * 0.45f);

    const ImVec4 pink(1.0f, 0.43f, 0.74f, 1.0f);
    const ImVec4 softPurple(0.82f, 0.50f, 0.98f, 1.0f);

    r = softPurple.x + (pink.x - softPurple.x) * phase;
    g = softPurple.y + (pink.y - softPurple.y) * phase;
    b = softPurple.z + (pink.z - softPurple.z) * phase;
}

void HUD::GetTesseractRGB(int offset, float& r, float& g, float& b) {
    const double tMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const float goldenRatio = 0.618033988749895f;
    const float baseHue = (float)fmod(tMs / 2000.0, 1.0);
    const float hueOffset = fmodf(offset * goldenRatio, 1.0f);
    const float hue = fmodf(baseHue + hueOffset, 1.0f);
    ImGui::ColorConvertHSVtoRGB(hue, 0.75f, 0.9f, r, g, b);
}

void HUD::GetTesseractHeaderRGB(int offset, float& r, float& g, float& b) {
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const float state = ceilf((float)(millis + offset) / 20.0f);
    const float hue = fmodf(state, 360.0f) / 360.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.8f, r, g, b);
}

std::vector<HUD::ModuleEntry> HUD::GetActiveModules(ImFont* nameFont, float nameFontSize, ImFont* tagFont, float tagFontSize) {
    std::vector<ModuleEntry> modules;

    ImFont* resolvedNameFont = nameFont ? nameFont : ImGui::GetFont();
    ImFont* resolvedTagFont = tagFont ? tagFont : resolvedNameFont;
    const float resolvedNameSize = resolvedNameFont ? nameFontSize : ImGui::GetFontSize();
    const float resolvedTagSize = resolvedTagFont ? tagFontSize : resolvedNameSize;
    const float spaceWidth = CalcTextSize(resolvedTagFont, resolvedTagSize, " ").x;

    for (const auto& mod : FeatureManager::Get()->GetAllModules()) {
        if (!mod->IsEnabled()) {
            continue;
        }

        const std::string name = mod->GetName();
        if (name == "ArrayList") {
            continue;
        }

        const std::string displayName = FormatModuleName(name, Bridge::Get()->GetConfig() && Bridge::Get()->GetConfig()->HUD.m_SpacedModules);
        const std::string tag = mod->GetTag();
        float width = CalcTextSize(resolvedNameFont, resolvedNameSize, displayName).x;
        if (!tag.empty()) {
            width += spaceWidth + CalcFormattedTagWidth(resolvedTagFont, resolvedTagSize, tag);
        }

        modules.push_back({ displayName, tag, width, mod->IsInUse() });
    }

    std::sort(modules.begin(), modules.end(), [](const ModuleEntry& a, const ModuleEntry& b) {
        return a.width > b.width;
    });

    return modules;
}

float HUD::CalcFormattedTagWidth(ImFont* font, float fontSize, const std::string& text) const {
    float width = 0.0f;
    for (size_t index = 0; index < text.size();) {
        char colorCode = 0;
        size_t consumed = 0;
        if (TryConsumeMinecraftColorCode(text, index, colorCode, consumed)) {
            index += consumed;
            continue;
        }

        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch < 0x80) {
            const char buffer[2] = { static_cast<char>(ch), '\0' };
            width += CalcTextSize(font, fontSize, buffer).x;
            ++index;
            continue;
        }

        int charLength = 1;
        if ((ch & 0xE0) == 0xC0) {
            charLength = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            charLength = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            charLength = 4;
        }

        if (index + static_cast<size_t>(charLength) > text.size()) {
            charLength = 1;
        }

        width += CalcTextSize(font, fontSize, text.substr(index, static_cast<size_t>(charLength))).x;
        index += static_cast<size_t>(charLength);
    }

    return width;
}

float HUD::DrawFormattedTag(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& pos, const std::string& text, ImU32 defaultColor, ImU32 shadowColor) {
    if (!drawList || text.empty()) {
        return 0.0f;
    }

    ImU32 currentColor = defaultColor;
    float currentX = pos.x;
    for (size_t index = 0; index < text.size();) {
        char colorCode = 0;
        size_t consumed = 0;
        if (TryConsumeMinecraftColorCode(text, index, colorCode, consumed)) {
            currentColor = McColorToImU32(colorCode);
            index += consumed;
            continue;
        }

        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch < 0x80) {
            const char buffer[2] = { static_cast<char>(ch), '\0' };
            const float charWidth = CalcTextSize(font, fontSize, buffer).x;
            drawList->AddText(font, fontSize, ImVec2(currentX + 1.0f, pos.y + 1.0f), shadowColor, buffer);
            drawList->AddText(font, fontSize, ImVec2(currentX, pos.y), currentColor, buffer);
            currentX += charWidth;
            ++index;
            continue;
        }

        int charLength = 1;
        if ((ch & 0xE0) == 0xC0) {
            charLength = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            charLength = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            charLength = 4;
        }

        if (index + static_cast<size_t>(charLength) > text.size()) {
            charLength = 1;
        }

        const std::string glyph = text.substr(index, static_cast<size_t>(charLength));
        const float charWidth = CalcTextSize(font, fontSize, glyph).x;
        drawList->AddText(font, fontSize, ImVec2(currentX + 1.0f, pos.y + 1.0f), shadowColor, glyph.c_str());
        drawList->AddText(font, fontSize, ImVec2(currentX, pos.y), currentColor, glyph.c_str());
        currentX += charWidth;
        index += static_cast<size_t>(charLength);
    }

    return currentX - pos.x;
}

void HUD::Render(ModuleConfig* config, float screenW, float screenH) {
    (void)screenH;

    if (!config || !config->HUD.m_Enabled) {
        return;
    }

    if (!ShouldRenderIngameHud()) {
        const auto now = std::chrono::steady_clock::now();
        m_FrameCount = 0;
        m_LastFpsTime = now;
        m_LastFrameTime = now;
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!drawList) {
        return;
    }

    ImFont* regularFont = m_RegularFont ? m_RegularFont : ImGui::GetFont();
    ImFont* boldFont = m_BoldFont ? m_BoldFont : regularFont;
    ImFont* vapeFont = m_VapeFont ? m_VapeFont : regularFont;
    if (!regularFont || !boldFont || !vapeFont) {
        return;
    }

    const bool riseMode = config->HUD.m_Mode == static_cast<int>(ArrayListMode::Rise);
    const bool tesseractMode = config->HUD.m_Mode == static_cast<int>(ArrayListMode::Tesseract);
    const bool vapeMode = config->HUD.m_Mode == static_cast<int>(ArrayListMode::VapeV4);
    const bool riseLikeMode = riseMode || vapeMode;
    ImFont* tagFont = vapeMode ? vapeFont : regularFont;
    ImFont* nameFont = vapeMode ? vapeFont : ((riseMode || tesseractMode) ? regularFont : boldFont);
    const float nameSize = nameFont->FontSize;
    const float tagSize = tagFont->FontSize;
    const float topY = 4.0f;
    const float lineHeight = (std::max)(tagSize, nameSize);
    const float moduleSpacing = riseLikeMode ? 0.0f : 2.0f;
    const float itemHeight = riseLikeMode ? (lineHeight + 6.0f) : (tesseractMode ? (nameSize + 4.0f) : (lineHeight + 2.0f));
    const float spaceWidth = CalcTextSize(tagFont, tagSize, " ").x;
    const ImU32 shadowColor = IM_COL32(0, 0, 0, 120);
    const ImU32 secondaryColor = riseLikeMode ? IM_COL32(154, 154, 154, 255) : IM_COL32(200, 200, 200, 255);
    const ImU32 riseBackgroundColor = IM_COL32(0, 0, 0, 88);
    const ImU32 riseWatermarkTextColor = IM_COL32(232, 232, 232, 255);
    const ImU32 vapeTagActiveColor = IM_COL32(255, 85, 85, 255);
    const float risePadX = 4.0f;
    const float risePadY = 2.0f;
    const float riseRectWidth = 2.0f;
    const float vapeLeftRounding = 6.0f;
    const ImU32 tesseractBackgroundColor = IM_COL32(25, 25, 30, 240);
    const float tesseractPadding = 6.0f;
    const float tesseractGapAfterText = 4.0f;
    const float tesseractRectWidth = 3.0f;
    const float tesseractMarginRight = 6.0f;
    const float defaultRightMargin = 6.0f;
    const float riseRightMargin = 4.0f;

    m_FrameCount++;
    const auto now = std::chrono::steady_clock::now();
    const float fpsElapsed = std::chrono::duration<float>(now - m_LastFpsTime).count();
    if (fpsElapsed >= 1.0f) {
        m_Fps = (int)(m_FrameCount / fpsElapsed);
        m_FrameCount = 0;
        m_LastFpsTime = now;
    }

    float deltaTime = std::chrono::duration<float>(now - m_LastFrameTime).count();
    m_LastFrameTime = now;
    if (deltaTime > 0.1f) {
        deltaTime = 0.1f;
    }

    if (config->HUD.m_Watermark) {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        if (riseMode) {
            GetRiseRGB(0, r, g, b);
        } else if (vapeMode) {
            GetVapeV4RGB(0, r, g, b);
        } else if (tesseractMode) {
            GetTesseractHeaderRGB(0, r, g, b);
        } else if (config->HUD.m_Rainbow) {
            GetRainbowRGB(0, r, g, b);
        }

        std::string nick = ResolvePlayerNick(config);
        if (nick.empty()) {
            nick = "player";
        }

        char fpsText[16];
        snprintf(fpsText, sizeof(fpsText), "%d FPS", m_Fps);

        const ImU32 accentColor = MakeColorU32(r, g, b);
        const std::string segment = " | " + nick + " | ";
        const float textY = riseLikeMode ? (topY + risePadY) : (tesseractMode ? 6.0f : topY);
        float cursorX = riseLikeMode ? (10.0f + risePadX) : 10.0f;

        const float titleWidth = CalcTextSize(nameFont, nameSize, "OpenCommunity").x;
        const float segmentWidth = CalcTextSize(tagFont, tagSize, segment).x;
        const float fpsWidth = CalcTextSize(tagFont, tagSize, fpsText).x;

        if (riseLikeMode) {
            const float boxWidth = titleWidth + segmentWidth + fpsWidth + risePadX * 2.0f + riseRectWidth + 1.0f;
            const ImVec2 boxMin(10.0f, topY);
            const ImVec2 boxMax(boxMin.x + boxWidth, topY + itemHeight);
            drawList->AddRectFilled(
                boxMin,
                boxMax,
                riseBackgroundColor,
                vapeMode ? vapeLeftRounding : 0.0f,
                vapeMode ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersNone);
            drawList->AddRectFilled(ImVec2(boxMax.x - riseRectWidth, boxMin.y), boxMax, accentColor, 0.0f);
        }

        DrawShadowedText(drawList, nameFont, nameSize, ImVec2(cursorX, textY), riseMode ? riseWatermarkTextColor : accentColor, shadowColor, "OpenCommunity");
        cursorX += titleWidth;

        DrawShadowedText(drawList, tagFont, tagSize, ImVec2(cursorX, textY), secondaryColor, shadowColor, segment);
        cursorX += segmentWidth;

        DrawShadowedText(drawList, tagFont, tagSize, ImVec2(cursorX, textY), secondaryColor, shadowColor, fpsText);
    }

    const auto modules = GetActiveModules(nameFont, nameSize, tagFont, tagSize);
    std::vector<std::string> activeKeys;
    activeKeys.reserve(modules.size());

    int index = 0;
    for (const auto& mod : modules) {
        float r;
        float g;
        float b;
        if (riseMode) {
            GetRiseRGB(index, r, g, b);
        } else if (vapeMode) {
            GetVapeV4RGB(index, r, g, b);
        } else if (tesseractMode) {
            GetTesseractRGB(index, r, g, b);
        } else if (config->HUD.m_Rainbow) {
            GetRainbowRGB(index * 400, r, g, b);
        } else {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            const float baseHue = fmodf((float)ms / 5000.0f, 1.0f);
            const float hueOffset = fmodf(index * 0.618033988749895f, 1.0f);
            const ImVec4 themed = color::GetCycleVec4(fmodf(baseHue + hueOffset, 1.0f));
            r = themed.x;
            g = themed.y;
            b = themed.z;
        }

        activeKeys.push_back(mod.name);

        float& progress = m_SlideProgress[mod.name];
        if (progress < 1.0f) {
            progress += deltaTime * 6.0f;
            if (progress > 1.0f) {
                progress = 1.0f;
            }
        }

        const float eased = 1.0f - powf(1.0f - progress, 3.0f);
        float layoutWidth = mod.width;
        if (tesseractMode) {
            layoutWidth = CalcTextSize(nameFont, nameSize, mod.name).x;
            if (!mod.tag.empty()) {
                layoutWidth += 4.0f + CalcFormattedTagWidth(tagFont, tagSize, mod.tag);
            }
        }

        const float boxWidth = riseLikeMode
            ? (layoutWidth + risePadX * 2.0f + riseRectWidth + 1.0f)
            : (tesseractMode ? (layoutWidth + tesseractPadding + tesseractGapAfterText + tesseractRectWidth) : layoutWidth);
        const float rightMargin = riseLikeMode ? riseRightMargin : (tesseractMode ? tesseractMarginRight : defaultRightMargin);
        const float targetX = screenW - boxWidth - rightMargin;
        const float currentX = screenW + (targetX - screenW) * eased;
        const float baseY = tesseractMode ? 2.0f : topY;
        const float currentY = baseY + index * (itemHeight + moduleSpacing);
        const ImU32 accentColor = MakeColorU32(r, g, b);
        const float textX = riseLikeMode ? (currentX + risePadX) : (tesseractMode ? (currentX + tesseractPadding) : currentX);
        const float textY = riseLikeMode ? (currentY + risePadY) : (tesseractMode ? (currentY + 2.0f) : currentY);

        if (riseLikeMode) {
            const ImVec2 boxMin(currentX, currentY);
            const ImVec2 boxMax(currentX + boxWidth, currentY + itemHeight);
            drawList->AddRectFilled(
                boxMin,
                boxMax,
                riseBackgroundColor,
                vapeMode ? vapeLeftRounding : 0.0f,
                vapeMode ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersNone);
            drawList->AddRectFilled(ImVec2(boxMax.x - riseRectWidth, boxMin.y), boxMax, accentColor, 0.0f);
        } else if (tesseractMode) {
            const ImVec2 boxMin(currentX, currentY);
            const ImVec2 boxMax(currentX + boxWidth, currentY + itemHeight);
            drawList->AddRectFilled(boxMin, boxMax, tesseractBackgroundColor, 5.0f, ImDrawFlags_RoundCornersLeft);
            drawList->AddRectFilled(ImVec2(boxMax.x - tesseractRectWidth, boxMin.y), boxMax, accentColor);
        }

        DrawShadowedText(drawList, nameFont, nameSize, ImVec2(textX, textY), accentColor, shadowColor, mod.name);

        if (!mod.tag.empty()) {
            const float tagX = textX + CalcTextSize(nameFont, nameSize, mod.name).x + (tesseractMode ? 4.0f : spaceWidth);
            const ImU32 tagColor = (vapeMode && mod.inUse) ? vapeTagActiveColor : secondaryColor;
            DrawFormattedTag(drawList, tagFont, tagSize, ImVec2(tagX, textY), mod.tag, tagColor, shadowColor);
        }

        index++;
    }

    for (auto it = m_SlideProgress.begin(); it != m_SlideProgress.end();) {
        if (std::find(activeKeys.begin(), activeKeys.end(), it->first) == activeKeys.end()) {
            it = m_SlideProgress.erase(it);
        } else {
            ++it;
        }
    }
}
