// tgfx_intern_string.h - String interning for termin-graphics
// Returns pointer valid for library lifetime. Thread-unsafe (call from main thread).
#pragma once

#include "tgfx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Intern a string. Returns a pointer to the interned copy.
// The returned pointer is valid for the library's lifetime.
// Returns NULL if s is NULL or allocation fails.
TGFX_API const char* tgfx_intern_string(const char* s);

// Free all interned strings. Call at shutdown.
TGFX_API void tgfx_intern_cleanup(void);

#ifdef __cplusplus
}
#endif
