#include "pch.h"
#include "HUD.h"
#include "../../core/Bridge.h"
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

void HUD::SetFonts(ImFont* regular, ImFont* bold) {
    m_RegularFont = regular;
    m_BoldFont = bold;
}

void HUD::GetRainbowRGB(int offset, float& r, float& g, float& b) {
    const auto now = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const ImVec4 themed = color::GetCycleVec4((float)((ms + offset) % 10000) / 10000.0f);
    r = themed.x;
    g = themed.y;
    b = themed.z;
}

std::vector<HUD::ModuleEntry> HUD::GetActiveModules() {
    std::vector<ModuleEntry> modules;

    ImFont* boldFont = m_BoldFont ? m_BoldFont : ImGui::GetFont();
    ImFont* regularFont = m_RegularFont ? m_RegularFont : ImGui::GetFont();
    const float boldSize = boldFont ? boldFont->FontSize : ImGui::GetFontSize();
    const float regularSize = regularFont ? regularFont->FontSize : ImGui::GetFontSize();
    const float spaceWidth = CalcTextSize(regularFont, regularSize, " ").x;

    for (const auto& mod : FeatureManager::Get()->GetAllModules()) {
        if (!mod->IsEnabled()) {
            continue;
        }

        const std::string name = mod->GetName();
        if (name == "ArrayList") {
            continue;
        }

        const std::string tag = mod->GetTag();
        float width = CalcTextSize(boldFont, boldSize, name).x;
        if (!tag.empty()) {
            width += spaceWidth + CalcTextSize(regularFont, regularSize, tag).x;
        }

        modules.push_back({ name, tag, width });
    }

    std::sort(modules.begin(), modules.end(), [](const ModuleEntry& a, const ModuleEntry& b) {
        return a.width > b.width;
    });

    return modules;
}

void HUD::Render(ModuleConfig* config, float screenW, float screenH) {
    (void)screenH;

    if (!config || !config->HUD.m_Enabled) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!drawList) {
        return;
    }

    ImFont* regularFont = m_RegularFont ? m_RegularFont : ImGui::GetFont();
    ImFont* boldFont = m_BoldFont ? m_BoldFont : regularFont;
    if (!regularFont || !boldFont) {
        return;
    }

    const float regularSize = regularFont->FontSize;
    const float boldSize = boldFont->FontSize;
    const float shadowOffset = 1.0f;
    const float topY = 4.0f;
    const float moduleSpacing = 2.0f;
    const float itemHeight = std::max(regularSize, boldSize) + 2.0f;
    const float spaceWidth = CalcTextSize(regularFont, regularSize, " ").x;
    const ImU32 shadowColor = IM_COL32(0, 0, 0, 120);
    const ImU32 secondaryColor = IM_COL32(200, 200, 200, 255);

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
        float r;
        float g;
        float b;
        if (config->HUD.m_Rainbow) {
            GetRainbowRGB(0, r, g, b);
        } else {
            r = 1.0f;
            g = 1.0f;
            b = 1.0f;
        }

        std::string nick = ResolvePlayerNick(config);
        if (nick.empty()) {
            nick = "player";
        }

        char fpsText[16];
        snprintf(fpsText, sizeof(fpsText), "%d FPS", m_Fps);

        const ImU32 accentColor = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
        const std::string segment = " | " + nick + " | ";
        const float textY = topY;
        float cursorX = 10.0f;

        drawList->AddText(boldFont, boldSize, ImVec2(cursorX + shadowOffset, textY + shadowOffset), shadowColor, "OpenCommunity");
        drawList->AddText(boldFont, boldSize, ImVec2(cursorX, textY), accentColor, "OpenCommunity");
        cursorX += CalcTextSize(boldFont, boldSize, "OpenCommunity").x;

        drawList->AddText(regularFont, regularSize, ImVec2(cursorX + shadowOffset, textY + shadowOffset), shadowColor, segment.c_str());
        drawList->AddText(regularFont, regularSize, ImVec2(cursorX, textY), secondaryColor, segment.c_str());
        cursorX += CalcTextSize(regularFont, regularSize, segment).x;

        drawList->AddText(regularFont, regularSize, ImVec2(cursorX + shadowOffset, textY + shadowOffset), shadowColor, fpsText);
        drawList->AddText(regularFont, regularSize, ImVec2(cursorX, textY), secondaryColor, fpsText);
    }

    const auto modules = GetActiveModules();
    std::vector<std::string> activeKeys;
    activeKeys.reserve(modules.size());

    int index = 0;
    for (const auto& mod : modules) {
        float r;
        float g;
        float b;
        if (config->HUD.m_Rainbow) {
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
        const float targetX = screenW - mod.width - 6.0f;
        const float currentX = screenW + (targetX - screenW) * eased;
        const float currentY = topY + index * (itemHeight + moduleSpacing);
        const ImU32 accentColor = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));

        drawList->AddText(boldFont, boldSize, ImVec2(currentX + shadowOffset, currentY + shadowOffset), shadowColor, mod.name.c_str());
        drawList->AddText(boldFont, boldSize, ImVec2(currentX, currentY), accentColor, mod.name.c_str());

        if (!mod.tag.empty()) {
            const float tagX = currentX + CalcTextSize(boldFont, boldSize, mod.name).x + spaceWidth;
            drawList->AddText(regularFont, regularSize, ImVec2(tagX + shadowOffset, currentY + shadowOffset), shadowColor, mod.tag.c_str());
            drawList->AddText(regularFont, regularSize, ImVec2(tagX, currentY), secondaryColor, mod.tag.c_str());
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
