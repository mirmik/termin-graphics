#pragma once

#include "tgfx_log.h"
#include <string>
#include <cstdio>
#include <cstdarg>
#include <exception>

namespace tgfx {

// Static logging class for C++ code
// Usage:
//   tgfx::Log::info("Loading asset: %s", name.c_str());
//   tgfx::Log::error(e, "Failed to load asset");
class Log {
public:
    template<typename... Args>
    static void debug(const char* format, Args... args) {
        tgfx_log_debug(format, args...);
    }

    template<typename... Args>
    static void info(const char* format, Args... args) {
        tgfx_log_info(format, args...);
    }

    template<typename... Args>
    static void warn(const char* format, Args... args) {
        tgfx_log_warn(format, args...);
    }

    template<typename... Args>
    static void error(const char* format, Args... args) {
        tgfx_log_error(format, args...);
    }

    static void debug(const std::string& msg) {
        tgfx_log_debug("%s", msg.c_str());
    }

    static void info(const std::string& msg) {
        tgfx_log_info("%s", msg.c_str());
    }

    static void warn(const std::string& msg) {
        tgfx_log_warn("%s", msg.c_str());
    }

    static void error(const std::string& msg) {
        tgfx_log_error("%s", msg.c_str());
    }

    // Exception logging with context
    static void debug(const std::exception& e, const char* context = nullptr) {
        log_exception(TGFX_LOG_DEBUG, e, context);
    }

    static void info(const std::exception& e, const char* context = nullptr) {
        log_exception(TGFX_LOG_INFO, e, context);
    }

    static void warn(const std::exception& e, const char* context = nullptr) {
        log_exception(TGFX_LOG_WARN, e, context);
    }

    static void error(const std::exception& e, const char* context = nullptr) {
        log_exception(TGFX_LOG_ERROR, e, context);
    }

    // Exception logging with format string context
    template<typename... Args>
    static void debug(const std::exception& e, const char* format, Args... args) {
        log_exception_fmt(TGFX_LOG_DEBUG, e, format, args...);
    }

    template<typename... Args>
    static void info(const std::exception& e, const char* format, Args... args) {
        log_exception_fmt(TGFX_LOG_INFO, e, format, args...);
    }

    template<typename... Args>
    static void warn(const std::exception& e, const char* format, Args... args) {
        log_exception_fmt(TGFX_LOG_WARN, e, format, args...);
    }

    template<typename... Args>
    static void error(const std::exception& e, const char* format, Args... args) {
        log_exception_fmt(TGFX_LOG_ERROR, e, format, args...);
    }

    static void set_level(tgfx_log_level level) {
        tgfx_log_set_level(level);
    }

    static void set_callback(tgfx_log_callback callback) {
        tgfx_log_set_callback(callback);
    }

private:
    static void log_exception(tgfx_log_level level, const std::exception& e, const char* context) {
        if (context) {
            tgfx_log(level, "%s: %s", context, e.what());
        } else {
            tgfx_log(level, "%s", e.what());
        }
    }

    template<typename... Args>
    static void log_exception_fmt(tgfx_log_level level, const std::exception& e, const char* format, Args... args) {
        char context[512];
        snprintf(context, sizeof(context), format, args...);
        tgfx_log(level, "%s: %s", context, e.what());
    }
};

} // namespace tgfx
