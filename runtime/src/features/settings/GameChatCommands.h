#pragma once

#include "CommandManager.h"

#include <string>
#include <vector>

namespace GameChatCommands {
    inline std::string ToUtf8(JNIEnv* env, jstring value) {
        if (!env || !value) {
            return {};
        }

        const char* chars = env->GetStringUTFChars(value, nullptr);
        if (!chars) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return {};
        }

        std::string text(chars);
        env->ReleaseStringUTFChars(value, chars);
        return text;
    }

    inline bool TryHandleText(JNIEnv* env, const std::string& text) {
        return CommandManager::TryHandleText(env, text);
    }

    inline bool TryHandle(JNIEnv* env, jstring message) {
        return TryHandleText(env, ToUtf8(env, message));
    }

    inline std::vector<std::string> CollectAutocompleteMatches(const std::string& text) {
        return CommandManager::CollectAutocompleteMatches(text);
    }
}
