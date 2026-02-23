// tgfx_log.h - Logging system for termin-graphics
#ifndef TGFX_LOG_H
#define TGFX_LOG_H

#include "tgfx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TGFX_LOG_DEBUG = 0,
    TGFX_LOG_INFO  = 1,
    TGFX_LOG_WARN  = 2,
    TGFX_LOG_ERROR = 3
} tgfx_log_level;

// Callback for log interception
typedef void (*tgfx_log_callback)(tgfx_log_level level, const char* message);

// Set callback for log interception (e.g. for editor console)
TGFX_API void tgfx_log_set_callback(tgfx_log_callback callback);

// Set minimum log level
TGFX_API void tgfx_log_set_level(tgfx_log_level min_level);

// Log with specified level (printf-style)
TGFX_API void tgfx_log(tgfx_log_level level, const char* format, ...);

TGFX_API void tgfx_log_debug(const char* format, ...);
TGFX_API void tgfx_log_info(const char* format, ...);
TGFX_API void tgfx_log_warn(const char* format, ...);
TGFX_API void tgfx_log_error(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif // TGFX_LOG_H
