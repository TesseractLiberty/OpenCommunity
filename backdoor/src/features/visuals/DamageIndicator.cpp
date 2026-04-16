#include "pch.h"
#include "DamageIndicator.h"

#include "../../game/jni/Class.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/jni/Method.h"
#include "../../game/mapping/Mapper.h"

#include <gl/GL.h>
#include <cctype>
#include <cfloat>

namespace {
    struct ArmorRenderData {
        jobject stack = nullptr;
        ImVec2 position{};
    };

    struct SkinTextureInfo {
        GLuint textureId = 0;
        int width = 64;
        int height = 64;
    };

    struct BackdropBlurTexture {
        GLuint textureId = 0;
        int width = 0;
        int height = 0;
        int captureWidth = 0;
        int captureHeight = 0;
        std::vector<unsigned char> pixels;
        std::vector<unsigned char> scratch;
        std::vector<unsigned char> capturePixels;
        ImVec2 uvMin = ImVec2(0.0f, 1.0f);
        ImVec2 uvMax = ImVec2(1.0f, 0.0f);
    };

    BackdropBlurTexture g_BackdropBlurTexture;

    float Clamp01(float value) {
        return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
    }

    float Wrap01(float value) {
        value = std::fmod(value, 1.0f);
        return value < 0.0f ? value + 1.0f : value;
    }

    float SnapPixel(float value) {
        return std::round(value);
    }

    ImU32 GetPercentageColor(float percent) {
        if (percent >= 0.70f) {
            return IM_COL32(102, 214, 112, 255);
        }
        if (percent >= 0.50f) {
            return IM_COL32(255, 219, 97, 255);
        }
        if (percent >= 0.20f) {
            return IM_COL32(255, 167, 72, 255);
        }
        return IM_COL32(255, 98, 98, 255);
    }

    ImVec4 ToColorVec4(const float* color, float alphaScale = 1.0f) {
        if (!color) {
            return ImVec4(1.0f, 1.0f, 1.0f, Clamp01(alphaScale));
        }

        return ImVec4(
            Clamp01(color[0]),
            Clamp01(color[1]),
            Clamp01(color[2]),
            Clamp01(color[3] * alphaScale));
    }

    ImVec4 MixColor(const ImVec4& first, const ImVec4& second, float t) {
        return ImVec4(
            first.x + (second.x - first.x) * t,
            first.y + (second.y - first.y) * t,
            first.z + (second.z - first.z) * t,
            first.w + (second.w - first.w) * t);
    }

    ImU32 ToColorU32(const float* color, float alphaScale = 1.0f) {
        return ImGui::ColorConvertFloat4ToU32(ToColorVec4(color, alphaScale));
    }

    ImU32 ToColorU32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            Clamp01(color.x),
            Clamp01(color.y),
            Clamp01(color.z),
            Clamp01(color.w)));
    }

    ImVec4 BuildPaletteColor(const float* color, float timeSeconds, float phase, float alphaScale = 1.0f) {
        const ImVec4 baseColor = ToColorVec4(color, alphaScale);

        float hue = 0.0f;
        float saturation = 0.0f;
        float value = 0.0f;
        ImGui::ColorConvertRGBtoHSV(baseColor.x, baseColor.y, baseColor.z, hue, saturation, value);

        const float waveA = 0.5f + 0.5f * std::sinf((timeSeconds * 1.85f) + phase);
        const float waveB = 0.5f + 0.5f * std::cosf((timeSeconds * 1.20f) + (phase * 1.37f));
        const float hueOffset = (0.012f + (saturation * 0.018f)) * std::sinf((timeSeconds * 1.10f) + phase);

        float outHue = Wrap01(hue + hueOffset);
        float outSaturation = Clamp01(saturation * (0.92f + 0.16f * waveA) + ((1.0f - saturation) * 0.05f * waveB));
        float outValue = Clamp01(value * (0.82f + 0.22f * waveB) + 0.08f);

        float red = 1.0f;
        float green = 1.0f;
        float blue = 1.0f;
        ImGui::ColorConvertHSVtoRGB(outHue, outSaturation, outValue, red, green, blue);
        return ImVec4(red, green, blue, baseColor.w);
    }

    void DrawShadowedText(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& pos, ImU32 color, const std::string& text) {
        if (!drawList || text.empty()) {
            return;
        }

        drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 155), text.c_str());
        drawList->AddText(font, fontSize, pos, color, text.c_str());
    }

    void DrawGrayBlur(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding) {
        if (!drawList) {
            return;
        }

        for (int layer = 18; layer >= 1; --layer) {
            const float spread = static_cast<float>(layer) * 1.35f;
            const float topSpread = spread * 0.70f;
            const float bottomSpread = spread * 1.20f;
            const float horizontalSpread = spread * 0.95f;
            const float yOffset = static_cast<float>(layer) * 0.18f;
            const int alpha = 2 + layer * 2;
            drawList->AddRectFilled(
                ImVec2(min.x - horizontalSpread, min.y - topSpread + yOffset),
                ImVec2(max.x + horizontalSpread, max.y + bottomSpread + yOffset),
                IM_COL32(68, 72, 79, alpha),
                rounding + spread);
        }
    }

    void ApplyBoxBlurPass(
        const std::vector<unsigned char>& source,
        std::vector<unsigned char>& destination,
        int width,
        int height,
        int radius,
        bool horizontal) {
        if (width <= 0 || height <= 0 || source.size() < static_cast<size_t>(width * height * 4)) {
            return;
        }

        destination.resize(source.size());

        if (radius <= 0) {
            destination = source;
            return;
        }

        const int channels = 4;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                unsigned int sums[4] = {};
                int sampleCount = 0;

                const int start = horizontal ? (std::max)(0, x - radius) : (std::max)(0, y - radius);
                const int end = horizontal ? (std::min)(width - 1, x + radius) : (std::min)(height - 1, y + radius);
                for (int sample = start; sample <= end; ++sample) {
                    const int sampleX = horizontal ? sample : x;
                    const int sampleY = horizontal ? y : sample;
                    const size_t sampleIndex = static_cast<size_t>((sampleY * width + sampleX) * channels);
                    for (int channel = 0; channel < channels; ++channel) {
                        sums[channel] += source[sampleIndex + channel];
                    }
                    ++sampleCount;
                }

                const size_t pixelIndex = static_cast<size_t>((y * width + x) * channels);
                for (int channel = 0; channel < channels; ++channel) {
                    destination[pixelIndex + channel] = static_cast<unsigned char>(sums[channel] / (std::max)(1, sampleCount));
                }
            }
        }
    }

    void DownsampleBox(
        const std::vector<unsigned char>& source,
        std::vector<unsigned char>& destination,
        int sourceWidth,
        int sourceHeight,
        int destinationWidth,
        int destinationHeight) {
        if (sourceWidth <= 0 || sourceHeight <= 0 || destinationWidth <= 0 || destinationHeight <= 0) {
            return;
        }

        destination.resize(static_cast<size_t>(destinationWidth * destinationHeight * 4));

        for (int y = 0; y < destinationHeight; ++y) {
            const int sourceY0 = (y * sourceHeight) / destinationHeight;
            const int sourceY1 = (std::max)(sourceY0 + 1, ((y + 1) * sourceHeight) / destinationHeight);
            for (int x = 0; x < destinationWidth; ++x) {
                const int sourceX0 = (x * sourceWidth) / destinationWidth;
                const int sourceX1 = (std::max)(sourceX0 + 1, ((x + 1) * sourceWidth) / destinationWidth);

                unsigned int sums[4] = {};
                int sampleCount = 0;

                for (int sourceY = sourceY0; sourceY < sourceY1; ++sourceY) {
                    for (int sourceX = sourceX0; sourceX < sourceX1; ++sourceX) {
                        const size_t sourceIndex = static_cast<size_t>((sourceY * sourceWidth + sourceX) * 4);
                        for (int channel = 0; channel < 4; ++channel) {
                            sums[channel] += source[sourceIndex + channel];
                        }
                        ++sampleCount;
                    }
                }

                const size_t destinationIndex = static_cast<size_t>((y * destinationWidth + x) * 4);
                for (int channel = 0; channel < 4; ++channel) {
                    destination[destinationIndex + channel] = static_cast<unsigned char>(sums[channel] / (std::max)(1, sampleCount));
                }
            }
        }
    }

    bool CaptureBackdropTexture(const ImVec2& min, const ImVec2& max, float scale) {
        const int captureWidth = (std::max)(1, static_cast<int>(std::ceil(max.x - min.x)));
        const int captureHeight = (std::max)(1, static_cast<int>(std::ceil(max.y - min.y)));
        if (captureWidth <= 1 || captureHeight <= 1) {
            return false;
        }

        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] <= 0 || viewport[3] <= 0) {
            return false;
        }

        const int padding = (std::max)(10, static_cast<int>(std::round(16.0f * scale)));
        const int paddedLeft = static_cast<int>(std::floor(min.x)) - padding;
        const int paddedTop = static_cast<int>(std::floor(min.y)) - padding;
        const int paddedRight = static_cast<int>(std::ceil(max.x)) + padding;
        const int paddedBottom = static_cast<int>(std::ceil(max.y)) + padding;

        const int screenX = (std::clamp)(paddedLeft, 0, (std::max)(0, viewport[2] - 1));
        const int screenYTop = (std::clamp)(paddedTop, 0, (std::max)(0, viewport[3] - 1));
        const int screenWidth = (std::min)((std::max)(1, paddedRight - paddedLeft), viewport[2] - screenX);
        const int screenHeight = (std::min)((std::max)(1, paddedBottom - paddedTop), viewport[3] - screenYTop);
        const int x = screenX;
        const int y = viewport[3] - (screenYTop + screenHeight);
        const int width = screenWidth;
        const int height = screenHeight;
        if (width <= 1 || height <= 1) {
            return false;
        }

        GLint previousTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

        if (g_BackdropBlurTexture.textureId == 0) {
            glGenTextures(1, &g_BackdropBlurTexture.textureId);
        }

        const int downsampleFactor = (scale >= 1.2f) ? 3 : 2;
        const int textureWidth = (std::max)(1, width / downsampleFactor);
        const int textureHeight = (std::max)(1, height / downsampleFactor);

        if (g_BackdropBlurTexture.captureWidth != width || g_BackdropBlurTexture.captureHeight != height) {
            g_BackdropBlurTexture.captureWidth = width;
            g_BackdropBlurTexture.captureHeight = height;
            g_BackdropBlurTexture.capturePixels.resize(static_cast<size_t>(width * height * 4));
        }

        if (g_BackdropBlurTexture.width != textureWidth || g_BackdropBlurTexture.height != textureHeight) {
            g_BackdropBlurTexture.width = textureWidth;
            g_BackdropBlurTexture.height = textureHeight;
            g_BackdropBlurTexture.pixels.resize(static_cast<size_t>(textureWidth * textureHeight * 4));
            g_BackdropBlurTexture.scratch.resize(static_cast<size_t>(textureWidth * textureHeight * 4));
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, g_BackdropBlurTexture.capturePixels.data());

        DownsampleBox(
            g_BackdropBlurTexture.capturePixels,
            g_BackdropBlurTexture.pixels,
            width,
            height,
            textureWidth,
            textureHeight);

        const int primaryRadius = (std::max)(3, static_cast<int>(std::round(5.0f * scale)));
        const int secondaryRadius = (std::max)(2, static_cast<int>(std::round(3.0f * scale)));
        ApplyBoxBlurPass(g_BackdropBlurTexture.pixels, g_BackdropBlurTexture.scratch, textureWidth, textureHeight, primaryRadius, true);
        ApplyBoxBlurPass(g_BackdropBlurTexture.scratch, g_BackdropBlurTexture.pixels, textureWidth, textureHeight, primaryRadius, false);
        ApplyBoxBlurPass(g_BackdropBlurTexture.pixels, g_BackdropBlurTexture.scratch, textureWidth, textureHeight, secondaryRadius, true);
        ApplyBoxBlurPass(g_BackdropBlurTexture.scratch, g_BackdropBlurTexture.pixels, textureWidth, textureHeight, secondaryRadius, false);

        glBindTexture(GL_TEXTURE_2D, g_BackdropBlurTexture.textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_BackdropBlurTexture.pixels.data());
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));

        const float captureMinX = static_cast<float>(screenX);
        const float captureMinY = static_cast<float>(screenYTop);
        g_BackdropBlurTexture.uvMin = ImVec2(
            Clamp01((min.x - captureMinX) / static_cast<float>(width)),
            Clamp01(1.0f - ((min.y - captureMinY) / static_cast<float>(height))));
        g_BackdropBlurTexture.uvMax = ImVec2(
            Clamp01((max.x - captureMinX) / static_cast<float>(width)),
            Clamp01(1.0f - ((max.y - captureMinY) / static_cast<float>(height))));
        return true;
    }

    void DrawBackdropBlur(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding, float scale) {
        if (!drawList || !CaptureBackdropTexture(min, max, scale) || g_BackdropBlurTexture.textureId == 0) {
            return;
        }

        const ImTextureID texture = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(g_BackdropBlurTexture.textureId));
        drawList->AddImageRounded(
            texture,
            min,
            max,
            g_BackdropBlurTexture.uvMin,
            g_BackdropBlurTexture.uvMax,
            IM_COL32(255, 255, 255, 232),
            rounding,
            ImDrawFlags_RoundCornersAll);
    }

    void DrawFallbackHead(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float scale, const std::string& name) {
        drawList->AddRectFilled(min, max, IM_COL32(43, 46, 54, 255), 6.0f * scale);

        if (name.empty()) {
            return;
        }

        char initial[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))), '\0' };
        const ImVec2 textSize = ImGui::CalcTextSize(initial);
        drawList->AddText(
            ImVec2(min.x + ((max.x - min.x) - textSize.x) * 0.5f, min.y + ((max.y - min.y) - textSize.y) * 0.5f),
            IM_COL32(238, 240, 243, 255),
            initial);
    }

    void DrawAccentGlow(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding, const float* color, float timeSeconds, float scale, float alphaScale) {
        if (!drawList || !color) {
            return;
        }

        const ImVec4 outerColor = BuildPaletteColor(color, timeSeconds, 0.35f, 0.16f * alphaScale);
        const ImVec4 midColor = BuildPaletteColor(color, timeSeconds, 1.65f, 0.14f * alphaScale);
        const ImVec4 innerColor = BuildPaletteColor(color, timeSeconds, 2.95f, 0.12f * alphaScale);

        drawList->AddRect(ImVec2(min.x - (4.0f * scale), min.y - (4.0f * scale)), ImVec2(max.x + (4.0f * scale), max.y + (4.0f * scale)), ToColorU32(outerColor), rounding + (4.0f * scale), 0, 1.0f * scale);
        drawList->AddRect(ImVec2(min.x - (2.5f * scale), min.y - (2.5f * scale)), ImVec2(max.x + (2.5f * scale), max.y + (2.5f * scale)), ToColorU32(midColor), rounding + (2.5f * scale), 0, 1.1f * scale);
        drawList->AddRect(ImVec2(min.x - scale, min.y - scale), ImVec2(max.x + scale, max.y + scale), ToColorU32(innerColor), rounding + scale, 0, 1.15f * scale);
    }

    SkinTextureInfo GetPlayerSkinTexture(JNIEnv* env, jobject playerObject, const std::string& playerName) {
        SkinTextureInfo textureInfo{};
        if (!env || !playerObject || !g_Game || !g_Game->IsInitialized()) {
            return textureInfo;
        }

        const std::string resourceLocationSignature = Mapper::Get("net/minecraft/util/ResourceLocation", 2);
        const std::string textureManagerSignature = Mapper::Get("net/minecraft/client/renderer/texture/TextureManager", 2);
        const std::string textureObjectSignature = Mapper::Get("net/minecraft/client/renderer/texture/ITextureObject", 2);
        if (resourceLocationSignature.empty() || textureManagerSignature.empty() || textureObjectSignature.empty()) {
            return textureInfo;
        }

        jobject skinLocation = nullptr;
        auto* playerClass = reinterpret_cast<Class*>(env->GetObjectClass(playerObject));
        if (playerClass) {
            const std::string methodName = Mapper::Get("getLocationSkin");
            Method* getLocationSkinMethod = methodName.empty() ? nullptr : playerClass->GetMethod(env, methodName.c_str(), ("()" + resourceLocationSignature).c_str());
            if (getLocationSkinMethod) {
                skinLocation = getLocationSkinMethod->CallObjectMethod(env, playerObject);
            }
            env->DeleteLocalRef(reinterpret_cast<jclass>(playerClass));
        }

        if (!skinLocation && !playerName.empty()) {
            const std::string className = Mapper::Get("net/minecraft/client/entity/AbstractClientPlayer");
            const std::string methodName = Mapper::Get("getLocationSkinByName");
            Class* abstractClientPlayerClass = (className.empty() || methodName.empty()) ? nullptr : g_Game->FindClass(className);
            Method* getLocationSkinByNameMethod = abstractClientPlayerClass ? abstractClientPlayerClass->GetMethod(env, methodName.c_str(), ("(Ljava/lang/String;)" + resourceLocationSignature).c_str(), true) : nullptr;
            if (getLocationSkinByNameMethod) {
                jstring jPlayerName = env->NewStringUTF(playerName.c_str());
                skinLocation = getLocationSkinByNameMethod->CallObjectMethod(env, abstractClientPlayerClass, true, jPlayerName);
                env->DeleteLocalRef(jPlayerName);
            }
        }

        if (!skinLocation) {
            return textureInfo;
        }

        jobject minecraft = Minecraft::GetTheMinecraft(env);
        if (!minecraft) {
            env->DeleteLocalRef(skinLocation);
            return textureInfo;
        }

        jobject textureManager = nullptr;
        auto* minecraftClass = reinterpret_cast<Class*>(env->GetObjectClass(minecraft));
        if (minecraftClass) {
            const std::string methodName = Mapper::Get("getTextureManager");
            Method* getTextureManagerMethod = methodName.empty() ? nullptr : minecraftClass->GetMethod(env, methodName.c_str(), ("()" + textureManagerSignature).c_str());
            if (getTextureManagerMethod) {
                textureManager = getTextureManagerMethod->CallObjectMethod(env, minecraft);
            }
            env->DeleteLocalRef(reinterpret_cast<jclass>(minecraftClass));
        }
        env->DeleteLocalRef(minecraft);

        if (!textureManager) {
            env->DeleteLocalRef(skinLocation);
            return textureInfo;
        }

        jobject textureObject = nullptr;
        auto* textureManagerClass = reinterpret_cast<Class*>(env->GetObjectClass(textureManager));
        if (textureManagerClass) {
            const std::string bindTextureName = Mapper::Get("bindTexture");
            Method* bindTextureMethod = bindTextureName.empty() ? nullptr : textureManagerClass->GetMethod(env, bindTextureName.c_str(), ("(" + resourceLocationSignature + ")V").c_str());
            if (bindTextureMethod) {
                bindTextureMethod->CallVoidMethod(env, textureManager, false, skinLocation);
            }

            const std::string getTextureName = Mapper::Get("getTexture");
            Method* getTextureMethod = getTextureName.empty() ? nullptr : textureManagerClass->GetMethod(env, getTextureName.c_str(), ("(" + resourceLocationSignature + ")" + textureObjectSignature).c_str());
            if (getTextureMethod) {
                textureObject = getTextureMethod->CallObjectMethod(env, textureManager, false, skinLocation);
            }

            env->DeleteLocalRef(reinterpret_cast<jclass>(textureManagerClass));
        }
        env->DeleteLocalRef(textureManager);
        env->DeleteLocalRef(skinLocation);

        if (!textureObject) {
            return textureInfo;
        }

        auto* textureObjectClass = reinterpret_cast<Class*>(env->GetObjectClass(textureObject));
        if (textureObjectClass) {
            const std::string methodName = Mapper::Get("getGlTextureId");
            Method* getTextureIdMethod = methodName.empty() ? nullptr : textureObjectClass->GetMethod(env, methodName.c_str(), "()I");
            if (getTextureIdMethod) {
                textureInfo.textureId = static_cast<GLuint>(getTextureIdMethod->CallIntMethod(env, textureObject));
            }
            env->DeleteLocalRef(reinterpret_cast<jclass>(textureObjectClass));
        }
        env->DeleteLocalRef(textureObject);

        if (textureInfo.textureId == 0 || !glIsTexture(textureInfo.textureId)) {
            textureInfo.textureId = 0;
            return textureInfo;
        }

        GLint previousTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glBindTexture(GL_TEXTURE_2D, textureInfo.textureId);

        GLint width = 0;
        GLint height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));

        textureInfo.width = width > 0 ? width : 64;
        textureInfo.height = height > 0 ? height : 64;
        return textureInfo;
    }

    void DrawPlayerHead(ImDrawList* drawList, JNIEnv* env, jobject playerObject, const ImVec2& min, const ImVec2& max, float scale, const std::string& playerName) {
        if (!drawList || !env || !playerObject) {
            DrawFallbackHead(drawList, min, max, scale, playerName);
            return;
        }

        const SkinTextureInfo textureInfo = GetPlayerSkinTexture(env, playerObject, playerName);
        if (textureInfo.textureId == 0 || textureInfo.width <= 0 || textureInfo.height <= 0) {
            DrawFallbackHead(drawList, min, max, scale, playerName);
            return;
        }

        const ImVec2 faceUvMin(8.0f / textureInfo.width, 8.0f / textureInfo.height);
        const ImVec2 faceUvMax(16.0f / textureInfo.width, 16.0f / textureInfo.height);
        const ImVec2 hatUvMin(40.0f / textureInfo.width, 8.0f / textureInfo.height);
        const ImVec2 hatUvMax(48.0f / textureInfo.width, 16.0f / textureInfo.height);
        const ImTextureID texture = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(textureInfo.textureId));
        const float rounding = 6.5f * scale;

        drawList->AddImageRounded(texture, min, max, faceUvMin, faceUvMax, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersAll);
        drawList->AddImageRounded(texture, min, max, hatUvMin, hatUvMax, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersAll);
    }

    void DrawChromaBar(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float fillRatio, float rounding, const float* accentColor, float timeSeconds) {
        if (!drawList) {
            return;
        }

        const ImVec2 snappedMin(SnapPixel(min.x), SnapPixel(min.y));
        const ImVec2 snappedMax(SnapPixel(max.x), SnapPixel(max.y));
        const float snappedRounding = (std::max)(1.0f, std::round(rounding));

        drawList->AddRectFilled(snappedMin, snappedMax, IM_COL32(27, 31, 38, 232), snappedRounding);
        drawList->AddRect(
            ImVec2(snappedMin.x + 0.5f, snappedMin.y + 0.5f),
            ImVec2(snappedMax.x - 0.5f, snappedMax.y - 0.5f),
            IM_COL32(92, 99, 112, 56),
            snappedRounding,
            0,
            1.0f);

        const ImVec2 fillMin(snappedMin.x + 1.0f, snappedMin.y + 1.0f);
        const ImVec2 fillMax(snappedMax.x - 1.0f, snappedMax.y - 1.0f);
        const float fillHeight = fillMax.y - fillMin.y;
        if (fillHeight <= 0.0f || fillMax.x <= fillMin.x) {
            return;
        }

        fillRatio = Clamp01(fillRatio);
        const float fillWidth = std::round((fillMax.x - fillMin.x) * fillRatio);
        if (fillWidth <= 0.0f) {
            return;
        }

        const ImVec4 leftColor = BuildPaletteColor(accentColor, timeSeconds, 0.35f);
        const ImVec4 centerColor = BuildPaletteColor(accentColor, timeSeconds, 1.55f);
        const ImVec4 rightColor = BuildPaletteColor(accentColor, timeSeconds, 2.75f);
        const float innerRounding = (std::max)(1.0f, snappedRounding - 1.0f);
        const float capWidth = (std::min)(innerRounding, fillWidth * 0.5f);
        const float fillRight = fillMin.x + fillWidth;
        const float innerMinX = fillMin.x + capWidth;
        const float innerMaxX = fillRight - capWidth;

        if (fillWidth <= (innerRounding * 2.0f)) {
            drawList->AddRectFilled(fillMin, ImVec2(fillRight, fillMax.y), ToColorU32(centerColor), innerRounding, ImDrawFlags_RoundCornersAll);
        } else {
            drawList->AddRectFilled(fillMin, ImVec2(fillMin.x + capWidth, fillMax.y), ToColorU32(leftColor), innerRounding, ImDrawFlags_RoundCornersLeft);

            const float middleWidth = innerMaxX - innerMinX;
            if (middleWidth > 0.0f) {
                const float splitX = std::round(innerMinX + (middleWidth * 0.5f));
                drawList->AddRectFilledMultiColor(
                    ImVec2(innerMinX, fillMin.y),
                    ImVec2(splitX, fillMax.y),
                    ToColorU32(leftColor),
                    ToColorU32(centerColor),
                    ToColorU32(centerColor),
                    ToColorU32(leftColor));
                drawList->AddRectFilledMultiColor(
                    ImVec2(splitX, fillMin.y),
                    ImVec2(innerMaxX, fillMax.y),
                    ToColorU32(centerColor),
                    ToColorU32(rightColor),
                    ToColorU32(rightColor),
                    ToColorU32(centerColor));
            }

            drawList->AddRectFilled(ImVec2(fillRight - capWidth, fillMin.y), ImVec2(fillRight, fillMax.y), ToColorU32(rightColor), innerRounding, ImDrawFlags_RoundCornersRight);
        }

        const float shineHeight = fillHeight * 0.42f;
        if (fillWidth > 2.0f && shineHeight > 1.0f) {
            drawList->AddRectFilledMultiColor(
                ImVec2(fillMin.x, fillMin.y),
                ImVec2(fillRight, fillMin.y + shineHeight),
                IM_COL32(255, 255, 255, 22),
                IM_COL32(255, 255, 255, 12),
                IM_COL32(255, 255, 255, 0),
                IM_COL32(255, 255, 255, 0));
        }
    }
}

ImFont* DamageIndicator::s_OpenSansRegularFont = nullptr;
ImFont* DamageIndicator::s_OpenSansBoldFont = nullptr;

void DamageIndicator::SetFonts(ImFont* regular, ImFont* bold) {
    s_OpenSansRegularFont = regular;
    s_OpenSansBoldFont = bold ? bold : regular;
}

void DamageIndicator::RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
    if (!IsEnabled() || !drawList || !g_Game || !g_Game->IsInitialized()) {
        return;
    }

    JNIEnv* env = g_Game->GetCurrentEnv();
    if (!env || env->PushLocalFrame(128) != 0) {
        return;
    }

    jobject currentScreenObject = Minecraft::GetCurrentScreen(env);
    if (currentScreenObject) {
        auto* screen = reinterpret_cast<GuiScreen*>(currentScreenObject);
        if (screen->IsInventory(env)) {
            env->PopLocalFrame(nullptr);
            return;
        }
    }

    jobject mouseOverObject = Minecraft::GetObjectMouseOver(env);
    if (!mouseOverObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* mouseOver = reinterpret_cast<MovingObjectPosition*>(mouseOverObject);
    if (!mouseOver->IsAimingEntity(env)) {
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject entityObject = mouseOver->GetEntity(env);
    if (!entityObject) {
        env->PopLocalFrame(nullptr);
        return;
    }

    const std::string playerClassName = Mapper::Get("net/minecraft/entity/player/EntityPlayer");
    Class* playerClass = playerClassName.empty() ? nullptr : g_Game->FindClass(playerClassName);
    if (!playerClass || !env->IsInstanceOf(entityObject, reinterpret_cast<jclass>(playerClass))) {
        env->PopLocalFrame(nullptr);
        return;
    }

    auto* target = reinterpret_cast<Player*>(entityObject);
    const std::string name = target->GetName(env, true);
    const float realHealth = target->GetRealHealth(env);
    if (name.empty() || realHealth < 0.0f) {
        env->PopLocalFrame(nullptr);
        return;
    }

    float maxHealth = target->GetMaxHealth(env);
    if (maxHealth <= 0.0f) {
        maxHealth = 20.0f;
    }
    maxHealth = (std::max)(maxHealth, realHealth);

    const float scale = GetScale();
    const ImVec2 anchor(
        GetAnchorX() * screenW,
        GetAnchorY() * screenH);

    MarkInUse(120);

    if (GetMode() == DamageIndicatorMode::Astralis) {
        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - m_LastAnimationTime).count();
        const float timeSeconds = static_cast<float>(ImGui::GetTime());
        m_LastAnimationTime = now;

        if (m_LastTargetName != name || m_AnimatedHealth < 0.0f) {
            m_LastTargetName = name;
            m_AnimatedHealth = realHealth;
        } else {
            const float lerpAmount = 1.0f - std::exp(-10.0f * (std::max)(deltaSeconds, 0.0f));
            m_AnimatedHealth += (realHealth - m_AnimatedHealth) * Clamp01(lerpAmount);
        }

        const float width = 232.0f * scale;
        const float height = 68.0f * scale;
        const float rounding = 12.0f * scale;
        const ImVec2 cardMin(anchor.x - width * 0.5f, anchor.y - height * 0.5f);
        const ImVec2 cardMax(cardMin.x + width, cardMin.y + height);
        const ImVec4 accentLeft = BuildPaletteColor(GetAccentColor(), timeSeconds, 0.15f);
        const ImVec4 accentRight = BuildPaletteColor(GetAccentColor(), timeSeconds, 1.55f);
        const ImVec4 accentCenter = BuildPaletteColor(GetAccentColor(), timeSeconds, 2.75f);
        const ImVec4 accentPrimary = MixColor(MixColor(accentLeft, accentRight, 0.5f), accentCenter, 0.45f);

        DrawBackdropBlur(drawList, cardMin, cardMax, rounding, scale);
        drawList->AddRectFilled(cardMin, cardMax, IM_COL32(18, 21, 27, 58), rounding);

        const float padding = 11.0f * scale;
        const float headSize = 44.0f * scale;
        const ImVec2 headMin(cardMin.x + padding, cardMin.y + (height - headSize) * 0.5f);
        const ImVec2 headMax(headMin.x + headSize, headMin.y + headSize);
        const float headRounding = 9.0f * scale;
        DrawAccentGlow(drawList, headMin, headMax, headRounding, GetAccentColor(), timeSeconds + 0.35f, scale, 0.85f);
        drawList->AddRectFilled(headMin, headMax, IM_COL32(18, 21, 27, 236), headRounding);
        drawList->AddRect(headMin, headMax, IM_COL32(146, 150, 160, 72), headRounding, 0, 1.0f * scale);
        drawList->AddRect(
            ImVec2(headMin.x + (0.5f * scale), headMin.y + (0.5f * scale)),
            ImVec2(headMax.x - (0.5f * scale), headMax.y - (0.5f * scale)),
            ToColorU32(accentPrimary),
            (std::max)(1.0f * scale, headRounding - (0.5f * scale)),
            0,
            1.35f * scale);

        const float headInset = 3.0f * scale;
        drawList->AddRectFilled(
            ImVec2(headMin.x + headInset, headMin.y + headInset),
            ImVec2(headMax.x - headInset, headMax.y - headInset),
            IM_COL32(11, 13, 16, 255),
            6.5f * scale);
        DrawPlayerHead(
            drawList,
            env,
            entityObject,
            ImVec2(headMin.x + headInset, headMin.y + headInset),
            ImVec2(headMax.x - headInset, headMax.y - headInset),
            scale,
            name);

        ImFont* nameFont = s_OpenSansBoldFont ? s_OpenSansBoldFont : ImGui::GetFont();
        ImFont* hpFont = s_OpenSansRegularFont ? s_OpenSansRegularFont : ImGui::GetFont();
        const float nameFontSize = 18.8f * scale;
        const float hpFontSize = 14.0f * scale;

        const float textX = headMax.x + 12.0f * scale;
        const float textWidth = cardMax.x - textX - padding;
        const float nameY = cardMin.y + 9.0f * scale;
        const float hpY = cardMin.y + 30.0f * scale;

        DrawShadowedText(drawList, nameFont, nameFontSize, ImVec2(textX, nameY), IM_COL32(245, 245, 245, 255), name);

        char healthText[32];
        std::snprintf(healthText, sizeof(healthText), "%.1f HP", realHealth);
        DrawShadowedText(drawList, hpFont, hpFontSize, ImVec2(textX, hpY), IM_COL32(214, 216, 220, 232), healthText);

        const float barHeight = 10.0f * scale;
        const float barRounding = barHeight * 0.5f;
        const float barY = cardMax.y - 17.0f * scale;
        const ImVec2 barMin(textX, barY);
        const ImVec2 barMax(textX + textWidth, barY + barHeight);
        const float animatedRatio = Clamp01(m_AnimatedHealth / maxHealth);
        DrawChromaBar(drawList, barMin, barMax, animatedRatio, barRounding, GetAccentColor(), timeSeconds);

        env->PopLocalFrame(nullptr);
        return;
    }

    const int health = static_cast<int>(std::roundf(realHealth));
    int roundedMaxHealth = static_cast<int>(std::roundf(maxHealth));
    if (roundedMaxHealth <= 0) {
        roundedMaxHealth = 20;
    }

    const float width = 250.0f * scale;
    const float height = 92.0f * scale;
    const ImVec2 origin(anchor.x - (width * 0.5f), anchor.y - (height * 0.5f));

    const float lineHeight = 18.0f * scale;
    const ImU32 shadowColor = IM_COL32(0, 0, 0, 180);
    const ImU32 textColor = IM_COL32(255, 255, 255, 255);
    const ImU32 healthColor = GetPercentageColor((std::clamp)(static_cast<float>(health) / static_cast<float>(roundedMaxHealth), 0.0f, 1.0f));

    const std::string headerText = "[" + name + "]";
    const ImVec2 namePos(origin.x, origin.y);
    drawList->AddText(nullptr, 16.0f * scale, ImVec2(namePos.x + 1.0f, namePos.y + 1.0f), shadowColor, headerText.c_str());
    drawList->AddText(nullptr, 16.0f * scale, namePos, textColor, headerText.c_str());

    char healthText[32];
    std::snprintf(healthText, sizeof(healthText), "%d", health);
    const ImVec2 healthPos(origin.x, origin.y + lineHeight);
    drawList->AddText(nullptr, 18.0f * scale, ImVec2(healthPos.x + 1.0f, healthPos.y + 1.0f), shadowColor, healthText);
    drawList->AddText(nullptr, 18.0f * scale, healthPos, healthColor, healthText);

    const ImVec2 healthTextSize = ImGui::GetFont()->CalcTextSizeA(18.0f * scale, FLT_MAX, 0.0f, healthText);
    const ImVec2 heartPos(origin.x + healthTextSize.x + 5.0f * scale, origin.y + lineHeight + 2.0f * scale);
    drawList->AddText(nullptr, 18.0f * scale, ImVec2(heartPos.x + 1.0f, heartPos.y + 1.0f), shadowColor, ICON_MD_FAVORITE);
    drawList->AddText(nullptr, 18.0f * scale, heartPos, IM_COL32(255, 50, 50, 255), ICON_MD_FAVORITE);

    std::vector<ArmorRenderData> armorItems;
    armorItems.reserve(4);

    float armorY = origin.y + (lineHeight * 2.0f);
    for (int index = 3; index >= 0; --index) {
        jobject armorObject = target->GetCurrentArmor(index, env);
        if (!armorObject) {
            armorY += lineHeight;
            continue;
        }

        auto* armor = reinterpret_cast<ItemStack*>(armorObject);
        armorItems.push_back({ armorObject, ImVec2(origin.x, armorY) });

        const int maxDamage = armor->GetMaxDamage(env);
        if (maxDamage > 0) {
            const int currentDurability = maxDamage - armor->GetItemDamage(env);
            const float percent = (std::clamp)(static_cast<float>(currentDurability) / static_cast<float>(maxDamage), 0.0f, 1.0f);

            char durabilityText[32];
            std::snprintf(durabilityText, sizeof(durabilityText), "(%d)", currentDurability);
            const ImVec2 durabilityPos(origin.x + 20.0f * scale, armorY + 1.0f * scale);
            drawList->AddText(nullptr, 14.0f * scale, ImVec2(durabilityPos.x + 1.0f, durabilityPos.y + 1.0f), shadowColor, durabilityText);
            drawList->AddText(nullptr, 14.0f * scale, durabilityPos, GetPercentageColor(percent), durabilityText);
        }

        armorY += lineHeight;
    }

    if (!armorItems.empty()) {
        jobject renderItemObject = Minecraft::GetRenderItem(env);
        if (renderItemObject) {
            auto* renderItem = reinterpret_cast<RenderItem*>(renderItemObject);

            glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            GLint viewport[4];
            glGetIntegerv(GL_VIEWPORT, viewport);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(0.0, viewport[2], viewport[3], 0.0, -1000.0, 1000.0);

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_LIGHTING);
            glEnable(GL_COLOR_MATERIAL);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            RenderHelper::EnableGUIStandardItemLighting(env);

            for (const auto& armorItem : armorItems) {
                glPushMatrix();
                glTranslatef(armorItem.position.x, armorItem.position.y, 0.0f);
                glScalef(scale, scale, 1.0f);
                renderItem->RenderItemIntoGUI(armorItem.stack, 0, 0, env);
                glPopMatrix();
            }

            RenderHelper::DisableStandardItemLighting(env);

            glDisable(GL_BLEND);
            glDisable(GL_COLOR_MATERIAL);
            glDisable(GL_LIGHTING);
            glDisable(GL_DEPTH_TEST);

            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();
            glPopAttrib();
        }
    }

    env->PopLocalFrame(nullptr);
}
