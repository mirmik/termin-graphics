// tc_handle.h - Generic handle type with generation for use-after-free protection
#pragma once

#include "tgfx/tgfx_api.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Generic handle - index + generation pair
// ============================================================================

typedef struct tc_handle {
    uint32_t index;       // Index into pool
    uint32_t generation;  // Generation for validation (detects use-after-free)
} tc_handle;

// Invalid handle constant (different syntax for C vs C++)
#ifdef __cplusplus
#define TC_HANDLE_INVALID (tc_handle{UINT32_MAX, 0})
#else
#define TC_HANDLE_INVALID ((tc_handle){UINT32_MAX, 0})
#endif

// Check if handle is invalid
static inline bool tc_handle_is_invalid(tc_handle h) {
    return h.index == UINT32_MAX;
}

// Check if two handles are equal
static inline bool tc_handle_eq(tc_handle a, tc_handle b) {
    return a.index == b.index && a.generation == b.generation;
}

// ============================================================================
// Typed handle macros - create type-safe handle aliases
// ============================================================================

#define TC_DEFINE_HANDLE(name) \
    typedef tc_handle name; \
    static inline name name##_invalid(void) { return TC_HANDLE_INVALID; } \
    static inline bool name##_is_invalid(name h) { return tc_handle_is_invalid(h); } \
    static inline bool name##_eq(name a, name b) { return tc_handle_eq(a, b); }

#ifdef __cplusplus
}
#endif
