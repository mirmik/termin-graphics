#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tgfx/tgfx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t tc_phase_mask;

enum tc_builtin_phase_index {
    TC_PHASE_INDEX_OPAQUE = 0,
    TC_PHASE_INDEX_TRANSPARENT = 1,
    TC_PHASE_INDEX_NORMAL = 2,
    TC_PHASE_INDEX_DEPTH = 3,
    TC_PHASE_INDEX_ID = 4,
    TC_PHASE_INDEX_SHADOW = 5,
    TC_PHASE_INDEX_UI = 6,
    TC_PHASE_INDEX_EDITOR = 7,
    TC_PHASE_INDEX_EDITOR_DEBUG = 8,
    TC_PHASE_INDEX_EDITOR_DEBUG_TRANSPARENT = 9,
    // Keep room for future engine phases without shifting project phase bits.
    TC_PHASE_INDEX_USER_FIRST = 16,
    TC_PHASE_INDEX_COUNT = 64,
};

#define TC_PHASE_NONE ((tc_phase_mask)0)
#define TC_PHASE_OPAQUE (UINT64_C(1) << TC_PHASE_INDEX_OPAQUE)
#define TC_PHASE_TRANSPARENT (UINT64_C(1) << TC_PHASE_INDEX_TRANSPARENT)
#define TC_PHASE_NORMAL (UINT64_C(1) << TC_PHASE_INDEX_NORMAL)
#define TC_PHASE_DEPTH (UINT64_C(1) << TC_PHASE_INDEX_DEPTH)
#define TC_PHASE_ID (UINT64_C(1) << TC_PHASE_INDEX_ID)
#define TC_PHASE_SHADOW (UINT64_C(1) << TC_PHASE_INDEX_SHADOW)
#define TC_PHASE_UI (UINT64_C(1) << TC_PHASE_INDEX_UI)
#define TC_PHASE_EDITOR (UINT64_C(1) << TC_PHASE_INDEX_EDITOR)
#define TC_PHASE_EDITOR_DEBUG (UINT64_C(1) << TC_PHASE_INDEX_EDITOR_DEBUG)
#define TC_PHASE_EDITOR_DEBUG_TRANSPARENT \
    (UINT64_C(1) << TC_PHASE_INDEX_EDITOR_DEBUG_TRANSPARENT)
#define TC_PHASE_ALL UINT64_MAX
#define TC_PHASE_PROJECT_CAPACITY \
    (TC_PHASE_INDEX_COUNT - TC_PHASE_INDEX_USER_FIRST)

// Resolves an existing engine or project phase. Returns TC_PHASE_NONE when the
// name is empty or has not been registered.
TGFX_API tc_phase_mask tc_phase_find(const char* name);

// Assigns a project-registry slot to a name. Project slot 0 maps to bit 16.
// Names must be configured explicitly before assets/passes are loaded.
// An empty name clears the slot. Duplicate, built-in, and invalid names fail.
TGFX_API bool tc_phase_set_project_name(uint32_t project_index, const char* name);

// Returns the canonical name for a single-bit phase, or NULL for zero,
// multi-bit masks, and unallocated project bits.
TGFX_API const char* tc_phase_name(tc_phase_mask phase);

TGFX_API bool tc_phase_is_single(tc_phase_mask phase);
TGFX_API bool tc_phase_mask_contains(tc_phase_mask mask, tc_phase_mask phase);

// Project/runtime teardown hook. Built-in phase assignments remain intact.
TGFX_API void tc_phase_clear_project_registry(void);

#ifdef __cplusplus
}
#endif
