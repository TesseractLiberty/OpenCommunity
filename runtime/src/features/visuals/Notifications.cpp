#include "pch.h"
#include "Notifications.h"

#include "../../game/classes/Minecraft.h"
#include "../../game/jni/GameInstance.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <mutex>
#include <vector>

namespace {
    constexpr float kCardWidth = 300.0f;
    constexpr float kCardPadding = 10.0f;
    constexpr float kCardGap = 10.0f;
    constexpr float kCardRounding = 5.0f;
    constexpr float kIconSize = 40.0f;
    constexpr float kIconRounding = 4.0f;
    constexpr float kColumnGap = 10.0f;
    constexpr float kHorizontalOffset = 15.0f;
    constexpr float kVerticalOffset = 5.0f;
    constexpr float kEnterDuration = 0.2f;
    constexpr float kExitDuration = 0.2f;
    constexpr float kLifetime = 3.0f;
    constexpr size_t kMaxNotifications = 8;

    struct NotificationEntry {
        std::uint64_t id = 0;
        std::uint64_t animationKey = 0;
        Notifications::Severity severity = Notifications::Severity::Info;
        Notifications::Severity previousSeverity = Notifications::Severity::Info;
        std::string title;
        std::string message;
        std::chrono::steady_clock::time_point createdAt;
        std::chrono::steady_clock::time_point refreshedAt;
        std::chrono::steady_clock::time_point severityChangedAt;
        float renderY = 0.0f;
        bool hasRenderY = false;
        bool hasPreviousSeverity = false;
    };

    std::mutex g_NotificationsMutex;
    std::vector<NotificationEntry> g_Notifications;
    std::atomic<std::uint64_t> g_NextNotificationId{ 1 };
    ImFont* g_InterRegularFont = nullptr;
    ImFont* g_InterBoldFont = nullptr;

    float Clamp01(float value) {
        return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
    }

    float EaseOutCubic(float value) {
        value = Clamp01(value);
        const float inverse = 1.0f - value;
        return 1.0f - inverse * inverse * inverse;
    }

    float SecondsSince(const std::chrono::steady_clock::time_point& timePoint) {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - timePoint).count();
    }

    bool IsToggleSeverity(Notifications::Severity severity) {
        return severity == Notifications::Severity::Enabled || severity == Notifications::Severity::Disabled;
    }

    ImU32 ColorWithAlpha(int r, int g, int b, int alpha, float alphaScale) {
        return IM_COL32(r, g, b, static_cast<int>((std::clamp)(alphaScale, 0.0f, 1.0f) * alpha));
    }

    ImVec2 CalcTextSize(ImFont* font, float fontSize, const std::string& text, float wrapWidth = 0.0f) {
        if (!font) {
            return ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);
        }

        return font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, text.c_str());
    }

    float CalcNotificationHeight(ImFont* titleFont, ImFont* messageFont, const NotificationEntry& notification) {
        const float textWidth = kCardWidth - (kCardPadding * 2.0f) - kIconSize - kColumnGap;
        const ImVec2 titleSize = CalcTextSize(titleFont, 14.0f, notification.title, textWidth);
        const ImVec2 messageSize = CalcTextSize(messageFont, 12.0f, notification.message, textWidth);
        const float textHeight = titleSize.y + 1.0f + messageSize.y;
        return (std::max)(kIconSize + (kCardPadding * 2.0f), textHeight + (kCardPadding * 2.0f));
    }

    bool ShouldRenderIngame() {
        if (!g_Game || !g_Game->IsInitialized()) {
            return false;
        }

        JNIEnv* env = g_Game->GetCurrentEnv();
        if (!env || env->PushLocalFrame(8) != 0) {
            return false;
        }

        jobject world = Minecraft::GetTheWorld(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            world = nullptr;
        }

        jobject player = Minecraft::GetThePlayer(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            player = nullptr;
        }

        jobject currentScreen = Minecraft::GetCurrentScreen(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            currentScreen = nullptr;
        }

        const bool shouldRender = world && player && !currentScreen;
        env->PopLocalFrame(nullptr);
        return shouldRender;
    }

    ImU32 GetSeverityColor(Notifications::Severity severity, float alpha) {
        switch (severity) {
        case Notifications::Severity::Success:
        case Notifications::Severity::Enabled:
            return ColorWithAlpha(77, 172, 104, 255, alpha);
        case Notifications::Severity::Error:
        case Notifications::Severity::Disabled:
            return ColorWithAlpha(252, 65, 48, 255, alpha);
        case Notifications::Severity::Info:
        default:
            return ColorWithAlpha(70, 119, 255, 255, alpha);
        }
    }

    ImVec4 GetSeverityColorVec4(Notifications::Severity severity, float alpha) {
        switch (severity) {
        case Notifications::Severity::Success:
        case Notifications::Severity::Enabled:
            return ImVec4(77.0f / 255.0f, 172.0f / 255.0f, 104.0f / 255.0f, Clamp01(alpha));
        case Notifications::Severity::Error:
        case Notifications::Severity::Disabled:
            return ImVec4(252.0f / 255.0f, 65.0f / 255.0f, 48.0f / 255.0f, Clamp01(alpha));
        case Notifications::Severity::Info:
        default:
            return ImVec4(70.0f / 255.0f, 119.0f / 255.0f, 255.0f / 255.0f, Clamp01(alpha));
        }
    }

    ImVec4 MixColor(const ImVec4& from, const ImVec4& to, float progress) {
        progress = Clamp01(progress);
        return ImVec4(
            from.x + (to.x - from.x) * progress,
            from.y + (to.y - from.y) * progress,
            from.z + (to.z - from.z) * progress,
            from.w + (to.w - from.w) * progress);
    }

    void DrawSuccessIcon(ImDrawList* drawList, const ImVec2& min, float alpha) {
        const ImU32 white = ColorWithAlpha(255, 255, 255, 255, alpha);
        const float x = min.x;
        const float y = min.y;
        drawList->AddLine(ImVec2(x + 12.0f, y + 21.0f), ImVec2(x + 18.0f, y + 27.0f), white, 3.0f);
        drawList->AddLine(ImVec2(x + 18.0f, y + 27.0f), ImVec2(x + 29.0f, y + 14.0f), white, 3.0f);
    }

    void DrawErrorIcon(ImDrawList* drawList, const ImVec2& min, float alpha) {
        const ImU32 white = ColorWithAlpha(255, 255, 255, 255, alpha);
        const float x = min.x;
        const float y = min.y;
        drawList->AddLine(ImVec2(x + 14.0f, y + 14.0f), ImVec2(x + 26.0f, y + 26.0f), white, 3.0f);
        drawList->AddLine(ImVec2(x + 26.0f, y + 14.0f), ImVec2(x + 14.0f, y + 26.0f), white, 3.0f);
    }

    void DrawInfoIcon(ImDrawList* drawList, const ImVec2& min, float alpha) {
        const ImU32 white = ColorWithAlpha(255, 255, 255, 255, alpha);
        const float x = min.x;
        const float y = min.y;
        drawList->AddLine(ImVec2(x + 21.0f, y + 10.0f), ImVec2(x + 19.0f, y + 25.0f), white, 3.0f);
        drawList->AddCircleFilled(ImVec2(x + 18.5f, y + 30.0f), 2.4f, white, 16);
    }

    void DrawToggleIcon(ImDrawList* drawList, const ImVec2& min, Notifications::Severity severity, float alpha, float progress = 1.0f) {
        const ImU32 white = ColorWithAlpha(255, 255, 255, 255, alpha);
        const float y = min.y + 20.0f;
        drawList->AddLine(ImVec2(min.x + 11.0f, y), ImVec2(min.x + 29.0f, y), white, 3.0f);

        const float disabledX = min.x + 15.2f;
        const float enabledX = min.x + 24.8f;
        const float knobX = severity == Notifications::Severity::Enabled
            ? disabledX + ((enabledX - disabledX) * Clamp01(progress))
            : enabledX + ((disabledX - enabledX) * Clamp01(progress));
        drawList->AddCircleFilled(ImVec2(knobX, y), 5.0f, white, 24);
    }

    void DrawSeverityIcon(
        ImDrawList* drawList,
        const ImVec2& min,
        Notifications::Severity severity,
        Notifications::Severity previousSeverity,
        bool hasPreviousSeverity,
        float severityProgress,
        float alpha) {
        const ImVec2 max(min.x + kIconSize, min.y + kIconSize);
        ImU32 iconColor = GetSeverityColor(severity, alpha);
        if (hasPreviousSeverity) {
            const ImVec4 previousColor = GetSeverityColorVec4(previousSeverity, alpha);
            const ImVec4 currentColor = GetSeverityColorVec4(severity, alpha);
            iconColor = ImGui::ColorConvertFloat4ToU32(MixColor(previousColor, currentColor, severityProgress));
        }
        drawList->AddRectFilled(min, max, iconColor, kIconRounding);

        switch (severity) {
        case Notifications::Severity::Success:
            DrawSuccessIcon(drawList, min, alpha);
            break;
        case Notifications::Severity::Error:
            DrawErrorIcon(drawList, min, alpha);
            break;
        case Notifications::Severity::Enabled:
        case Notifications::Severity::Disabled:
            DrawToggleIcon(drawList, min, severity, alpha, hasPreviousSeverity ? severityProgress : 1.0f);
            break;
        case Notifications::Severity::Info:
        default:
            DrawInfoIcon(drawList, min, alpha);
            break;
        }
    }

    void DrawNotification(ImDrawList* drawList, const NotificationEntry& notification, const ImVec2& min, float height, float alpha) {
        const ImVec2 max(min.x + kCardWidth, min.y + height);
        const ImU32 background = ColorWithAlpha(0, 0, 0, 173, alpha);
        const ImU32 titleColor = ColorWithAlpha(255, 255, 255, 255, alpha);
        const ImU32 messageColor = ColorWithAlpha(211, 211, 211, 255, alpha);

        drawList->AddRectFilled(min, max, background, kCardRounding);

        const ImVec2 iconMin(min.x + kCardPadding, min.y + kCardPadding);
        const float severityProgress = EaseOutCubic(SecondsSince(notification.severityChangedAt) / kEnterDuration);
        DrawSeverityIcon(
            drawList,
            iconMin,
            notification.severity,
            notification.previousSeverity,
            notification.hasPreviousSeverity,
            severityProgress,
            alpha);

        ImFont* titleFont = g_InterBoldFont ? g_InterBoldFont : ImGui::GetFont();
        ImFont* messageFont = g_InterRegularFont ? g_InterRegularFont : ImGui::GetFont();
        const float textX = iconMin.x + kIconSize + kColumnGap;
        const float textWidth = kCardWidth - (kCardPadding * 2.0f) - kIconSize - kColumnGap;
        const ImVec2 titlePos(textX, min.y + kCardPadding - 1.0f);
        const ImVec2 messagePos(textX, min.y + kCardPadding + 18.0f);

        drawList->AddText(titleFont, 14.0f, titlePos, titleColor, notification.title.c_str(), nullptr, textWidth);
        drawList->AddText(messageFont, 12.0f, messagePos, messageColor, notification.message.c_str(), nullptr, textWidth);
    }
}

void Notifications::SendNotifications::SUCCESS(const std::string& message, const std::string& title) {
    Notifications::SendSuccess(message, title);
}

void Notifications::SendNotifications::ERROR(const std::string& message, const std::string& title) {
    Notifications::SendError(message, title);
}

void Notifications::SendNotifications::INFO(const std::string& message, const std::string& title) {
    Notifications::SendInfo(message, title);
}

void Notifications::SendNotifications::ENABLED(const std::string& moduleName, const std::string& title) {
    Notifications::SendEnabled(moduleName, title);
}

void Notifications::SendNotifications::DISABLED(const std::string& moduleName, const std::string& title) {
    Notifications::SendDisabled(moduleName, title);
}

void Notifications::SendNotification(Severity severity, const std::string& title, const std::string& message) {
    if (message.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t id = g_NextNotificationId.fetch_add(1, std::memory_order_relaxed);
    NotificationEntry entry;
    entry.id = id;
    entry.animationKey = id;
    entry.severity = severity;
    entry.previousSeverity = severity;
    entry.title = title.empty() ? "Notification" : title;
    entry.message = message;
    entry.createdAt = now;
    entry.refreshedAt = now;
    entry.severityChangedAt = now;

    std::lock_guard<std::mutex> lock(g_NotificationsMutex);

    if (IsToggleSeverity(severity)) {
        auto existing = std::find_if(g_Notifications.begin(), g_Notifications.end(), [&message](const NotificationEntry& notification) {
            return IsToggleSeverity(notification.severity) && notification.message == message;
        });

        if (existing != g_Notifications.end()) {
            existing->previousSeverity = existing->severity;
            existing->severity = severity;
            existing->title = entry.title;
            existing->refreshedAt = now;
            existing->severityChangedAt = now;
            existing->hasPreviousSeverity = true;
            return;
        }
    }

    g_Notifications.insert(g_Notifications.begin(), std::move(entry));

    if (g_Notifications.size() > kMaxNotifications) {
        g_Notifications.resize(kMaxNotifications);
    }
}

void Notifications::SendSuccess(const std::string& message, const std::string& title) {
    SendNotification(Severity::Success, title, message);
}

void Notifications::SendError(const std::string& message, const std::string& title) {
    SendNotification(Severity::Error, title, message);
}

void Notifications::SendInfo(const std::string& message, const std::string& title) {
    SendNotification(Severity::Info, title, message);
}

void Notifications::SendEnabled(const std::string& moduleName, const std::string& title) {
    SendNotification(Severity::Enabled, title, moduleName);
}

void Notifications::SendDisabled(const std::string& moduleName, const std::string& title) {
    SendNotification(Severity::Disabled, title, moduleName);
}

#ifdef _RUNTIME
void Notifications::SetFonts(ImFont* regular, ImFont* bold) {
    g_InterRegularFont = regular;
    g_InterBoldFont = bold ? bold : regular;
}

void Notifications::RenderOverlay(ImDrawList* drawList, float screenW, float screenH) {
    if (!IsEnabled() || !drawList || screenW <= 0.0f || screenH <= 0.0f || !ShouldRenderIngame()) {
        return;
    }

    ImFont* titleFont = g_InterBoldFont ? g_InterBoldFont : ImGui::GetFont();
    ImFont* messageFont = g_InterRegularFont ? g_InterRegularFont : ImGui::GetFont();
    if (!titleFont || !messageFont) {
        return;
    }

    static auto lastFrameTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    float deltaSeconds = std::chrono::duration<float>(now - lastFrameTime).count();
    lastFrameTime = now;
    deltaSeconds = (std::clamp)(deltaSeconds, 0.0f, 0.1f);

    std::lock_guard<std::mutex> lock(g_NotificationsMutex);

    g_Notifications.erase(
        std::remove_if(g_Notifications.begin(), g_Notifications.end(), [](const NotificationEntry& notification) {
            return SecondsSince(notification.refreshedAt) >= (kLifetime + kExitDuration);
        }),
        g_Notifications.end());

    if (g_Notifications.empty()) {
        return;
    }

    MarkInUse(120);

    std::vector<float> heights;
    heights.reserve(g_Notifications.size());

    float totalHeight = 0.0f;
    for (const auto& notification : g_Notifications) {
        const float height = CalcNotificationHeight(titleFont, messageFont, notification);
        heights.push_back(height);
        totalHeight += height + kCardGap;
    }

    if (!heights.empty()) {
        totalHeight -= kCardGap;
    }

    const float baseX = screenW - kHorizontalOffset - kCardWidth;
    float targetY = screenH - kVerticalOffset - totalHeight;

    for (size_t index = 0; index < g_Notifications.size(); ++index) {
        auto& notification = g_Notifications[index];
        const float height = heights[index];
        const float boxAge = SecondsSince(notification.createdAt);
        const float lifetimeAge = SecondsSince(notification.refreshedAt);

        float alpha = 1.0f;
        float offsetX = 0.0f;
        if (boxAge < kEnterDuration) {
            const float progress = EaseOutCubic(boxAge / kEnterDuration);
            alpha = progress;
            offsetX = 30.0f * (1.0f - progress);
        } else if (lifetimeAge >= kLifetime) {
            const float progress = EaseOutCubic((lifetimeAge - kLifetime) / kExitDuration);
            alpha = 1.0f - progress;
            offsetX = 30.0f * progress;
        }

        if (!notification.hasRenderY) {
            notification.renderY = targetY;
            notification.hasRenderY = true;
        } else {
            const float flipProgress = 1.0f - std::exp(-12.0f * deltaSeconds);
            notification.renderY += (targetY - notification.renderY) * Clamp01(flipProgress);
        }

        DrawNotification(drawList, notification, ImVec2(baseX + offsetX, notification.renderY), height, alpha);
        targetY += height + kCardGap;
    }
}
#endif
