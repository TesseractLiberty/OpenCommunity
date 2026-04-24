#pragma once

#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../shared/common/logging/Logger.h"
#include "../../core/Bridge.h"
#include "../../game/classes/Minecraft.h"
#include "../../game/jni/Class.h"
#include "../../game/jni/Field.h"
#include "../../game/jni/GameInstance.h"
#include "../../game/jni/JniRefs.h"
#include "../../game/jni/Method.h"
#include "../../game/mapping/Mapper.h"
#include "../visuals/Notifications.h"

#include <string>

namespace CommandOutput {
    enum class Severity {
        Info,
        Success,
        Error
    };

    namespace detail {
        inline std::string StripMarkdownEmphasis(const std::string& value) {
            std::string sanitized;
            sanitized.reserve(value.size());

            for (size_t index = 0; index < value.size(); ++index) {
                const char character = value[index];
                if (character == '*' && index + 1 < value.size() && value[index + 1] == '*') {
                    ++index;
                    continue;
                }

                sanitized.push_back(character);
            }

            return sanitized;
        }

        inline std::string FormatChatPrefix(Severity severity, const std::string& title) {
            const char* section = "\xC2\xA7";
            const char* accent = "b";
            const char* icon = "*";
            switch (severity) {
            case Severity::Success:
                accent = "a";
                icon = "+";
                break;
            case Severity::Error:
                accent = "c";
                icon = "!";
                break;
            case Severity::Info:
            default:
                accent = "6";
                icon = "\xE2\x98\x85";
                break;
            }

            std::string prefix;
            prefix.reserve(48 + title.size());
            prefix += section; prefix += "8[";
            prefix += section; prefix += accent;
            prefix += icon;
            prefix += section; prefix += "8] ";
            prefix += section; prefix += "b";
            prefix += title.empty() ? "Information" : title;
            prefix += section; prefix += "8 ";
            prefix += section; prefix += "f";
            return prefix;
        }

        inline bool PrintLocalChat(JNIEnv* env, const std::string& formattedMessage) {
            if (!env || !g_Game || !g_Game->IsInitialized() || formattedMessage.empty()) {
                return false;
            }

            JniLocalFrame frame(env, 32);
            if (!frame.IsActive()) {
                return false;
            }

            Class* minecraftClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/Minecraft"));
            Class* guiIngameClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/gui/GuiIngame"));
            Class* guiNewChatClass = g_Game->FindClass(Mapper::Get("net/minecraft/client/gui/GuiNewChat"));
            Class* chatComponentTextClass = g_Game->FindClass(Mapper::Get("net/minecraft/util/ChatComponentText"));
            if (!minecraftClass || !guiIngameClass || !guiNewChatClass || !chatComponentTextClass) {
                return false;
            }

            Field* ingameGuiField = minecraftClass->GetField(
                env,
                Mapper::Get("ingameGUI").c_str(),
                Mapper::Get("net/minecraft/client/gui/GuiIngame", 2).c_str());
            Method* getChatGuiMethod = guiIngameClass->GetMethod(
                env,
                Mapper::Get("getChatGUI").c_str(),
                Mapper::Get("net/minecraft/client/gui/GuiNewChat", 3).c_str());
            Method* printChatMessageMethod = guiNewChatClass->GetMethod(
                env,
                Mapper::Get("printChatMessage").c_str(),
                ("(" + Mapper::Get("net/minecraft/util/IChatComponent", 2) + ")V").c_str());
            if (!ingameGuiField || !getChatGuiMethod || !printChatMessageMethod) {
                return false;
            }

            jmethodID chatComponentCtor = env->GetMethodID(
                reinterpret_cast<jclass>(chatComponentTextClass),
                "<init>",
                "(Ljava/lang/String;)V");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            if (!chatComponentCtor) {
                return false;
            }

            JniLocalRef<jobject> minecraft(env, Minecraft::GetTheMinecraft(env));
            if (!minecraft) {
                return false;
            }

            JniLocalRef<jobject> ingameGui(env, ingameGuiField->GetObjectField(env, minecraft.Get()));
            if (!ingameGui) {
                return false;
            }

            JniLocalRef<jobject> chatGui(env, getChatGuiMethod->CallObjectMethod(env, ingameGui.Get()));
            if (!chatGui) {
                return false;
            }

            JniLocalRef<jstring> text(env, env->NewStringUTF(formattedMessage.c_str()));
            if (!text) {
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                return false;
            }

            JniLocalRef<jobject> component(
                env,
                env->NewObject(reinterpret_cast<jclass>(chatComponentTextClass), chatComponentCtor, text.Get()));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            if (!component) {
                return false;
            }

            printChatMessageMethod->CallVoidMethod(env, chatGui.Get(), false, component.Get());
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return false;
            }

            return true;
        }
    }

    inline void Send(JNIEnv* env, Severity severity, const std::string& title, const std::string& message) {
        auto* bridge = Bridge::Get();
        ModuleConfig* config = bridge ? bridge->GetConfig() : nullptr;
        const bool preferChat = config &&
            config->GameChat.m_OutputMode == static_cast<int>(GameChatOutputMode::Chat);
        const bool notificationsAvailable = !config || config->Notifications.m_Enabled;

        const std::string formattedChatMessage = detail::FormatChatPrefix(severity, title) + detail::StripMarkdownEmphasis(message);
        if (preferChat || !notificationsAvailable) {
            if (detail::PrintLocalChat(env, formattedChatMessage)) {
                return;
            }
        }

        switch (severity) {
        case Severity::Success:
            Notifications::SendSuccess(message, title.empty() ? "Success" : title);
            return;
        case Severity::Error:
            Notifications::SendError(message, title.empty() ? "Error" : title);
            return;
        case Severity::Info:
        default:
            Notifications::SendInfo(message, title.empty() ? "Information" : title);
            return;
        }
    }

    inline void SendInfo(JNIEnv* env, const std::string& message, const std::string& title = "Information") {
        Send(env, Severity::Info, title, message);
    }

    inline void SendSuccess(JNIEnv* env, const std::string& message, const std::string& title = "Success") {
        Send(env, Severity::Success, title, message);
    }

    inline void SendError(JNIEnv* env, const std::string& message, const std::string& title = "Error") {
        Send(env, Severity::Error, title, message);
    }
}
