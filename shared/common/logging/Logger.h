#pragma once

#include <cstdarg>
#include <cstdio>
#include <windows.h>

#include "../security/XorString.h"

namespace OpenCommunity {
    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    inline const char* ToString(LogLevel level) {
        switch (level) {
        case LogLevel::Debug:
            return XOR("Debug");
        case LogLevel::Info:
            return XOR("Info");
        case LogLevel::Warning:
            return XOR("Warning");
        case LogLevel::Error:
            return XOR("Error");
        default:
            return XOR("Unknown");
        }
    }

    inline void Log(LogLevel level, const char* component, const char* message) {
        char buffer[1400] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            XOR("[OpenCommunity][%s][%s] %s\r\n"),
            ToString(level),
            component ? component : XOR("Core"),
            message ? message : "");
        OutputDebugStringA(buffer);
        MemoryScrub::Wipe(buffer, sizeof(buffer));
    }

    inline void LogFormat(LogLevel level, const char* component, const char* format, ...) {
        char message[1024] = {};

        va_list args;
        va_start(args, format);
        std::vsnprintf(message, sizeof(message), format ? format : "", args);
        va_end(args);

        message[sizeof(message) - 1] = '\0';
        Log(level, component, message);
        MemoryScrub::Wipe(message, sizeof(message));
    }
}

#define OC_LOG_DEBUG(component, message) ::OpenCommunity::Log(::OpenCommunity::LogLevel::Debug, component, message)
#define OC_LOG_INFO(component, message) ::OpenCommunity::Log(::OpenCommunity::LogLevel::Info, component, message)
#define OC_LOG_WARNING(component, message) ::OpenCommunity::Log(::OpenCommunity::LogLevel::Warning, component, message)
#define OC_LOG_ERROR(component, message) ::OpenCommunity::Log(::OpenCommunity::LogLevel::Error, component, message)

#define OC_LOG_DEBUGF(component, format, ...) ::OpenCommunity::LogFormat(::OpenCommunity::LogLevel::Debug, component, format, __VA_ARGS__)
#define OC_LOG_INFOF(component, format, ...) ::OpenCommunity::LogFormat(::OpenCommunity::LogLevel::Info, component, format, __VA_ARGS__)
#define OC_LOG_WARNINGF(component, format, ...) ::OpenCommunity::LogFormat(::OpenCommunity::LogLevel::Warning, component, format, __VA_ARGS__)
#define OC_LOG_ERRORF(component, format, ...) ::OpenCommunity::LogFormat(::OpenCommunity::LogLevel::Error, component, format, __VA_ARGS__)
