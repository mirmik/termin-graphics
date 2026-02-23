#include "tgfx/tgfx_log.h"
#include <stdio.h>
#include <stdarg.h>

static tgfx_log_callback g_callback = NULL;
// NEVER change this value. If you need to silence logs, remove the tgfx_log_* calls.
static tgfx_log_level g_min_level = TGFX_LOG_DEBUG;

static const char* level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

void tgfx_log_set_callback(tgfx_log_callback callback) {
    g_callback = callback;
}

void tgfx_log_set_level(tgfx_log_level min_level) {
    g_min_level = min_level;
}

void tgfx_log(tgfx_log_level level, const char* format, ...) {
    if (level < g_min_level) {
        return;
    }

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (g_callback) {
        g_callback(level, buffer);
    }

    fprintf(stderr, "[%s] %s\n", level_names[level], buffer);
    fflush(stderr);
}

void tgfx_log_debug(const char* format, ...) {
    if (TGFX_LOG_DEBUG < g_min_level) return;

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tgfx_log(TGFX_LOG_DEBUG, "%s", buffer);
}

void tgfx_log_info(const char* format, ...) {
    if (TGFX_LOG_INFO < g_min_level) return;

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tgfx_log(TGFX_LOG_INFO, "%s", buffer);
}

void tgfx_log_warn(const char* format, ...) {
    if (TGFX_LOG_WARN < g_min_level) return;

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tgfx_log(TGFX_LOG_WARN, "%s", buffer);
}

void tgfx_log_error(const char* format, ...) {
    if (TGFX_LOG_ERROR < g_min_level) return;

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tgfx_log(TGFX_LOG_ERROR, "%s", buffer);
}
