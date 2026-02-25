// tc_material_registry.h - Global material storage with pool + hash table
#pragma once

#include "tgfx/resources/tc_material.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Lifecycle (called from tc_init/tc_shutdown)
// ============================================================================

TGFX_API void tc_material_init(void);
TGFX_API void tc_material_shutdown(void);

// ============================================================================
// Material operations (handle-based API)
// ============================================================================

TGFX_API tc_material_handle tc_material_create(const char* uuid, const char* name);
TGFX_API tc_material_handle tc_material_find(const char* uuid);
TGFX_API tc_material_handle tc_material_find_by_name(const char* name);
TGFX_API tc_material_handle tc_material_get_or_create(const char* uuid, const char* name);
TGFX_API tc_material* tc_material_get(tc_material_handle h);

static inline const char* tc_material_uuid(tc_material_handle h) {
    tc_material* m = tc_material_get(h);
    return m ? m->header.uuid : NULL;
}

static inline const char* tc_material_name(tc_material_handle h) {
    tc_material* m = tc_material_get(h);
    return m ? m->header.name : NULL;
}

TGFX_API bool tc_material_is_valid(tc_material_handle h);
TGFX_API bool tc_material_destroy(tc_material_handle h);
TGFX_API bool tc_material_contains(const char* uuid);
TGFX_API size_t tc_material_count(void);

// ============================================================================
// Phase operations
// ============================================================================

TGFX_API tc_material_phase* tc_material_add_phase(
    tc_material* mat,
    tc_shader_handle shader,
    const char* phase_mark,
    int priority
);

TGFX_API bool tc_material_remove_phase(tc_material* mat, size_t index);

TGFX_API size_t tc_material_get_phases_for_mark(
    tc_material* mat,
    const char* mark,
    tc_material_phase** out_phases,
    size_t max_count
);

// ============================================================================
// Material info for debugging/inspection
// ============================================================================

typedef struct tc_material_info {
    tc_material_handle handle;
    char uuid[40];
    const char* name;
    uint32_t ref_count;
    uint32_t version;
    size_t phase_count;
    size_t texture_count;
} tc_material_info;

TGFX_API tc_material_info* tc_material_get_all_info(size_t* count);

// ============================================================================
// Iteration
// ============================================================================

typedef bool (*tc_material_iter_fn)(tc_material_handle h, tc_material* mat, void* user_data);
TGFX_API void tc_material_foreach(tc_material_iter_fn callback, void* user_data);

// ============================================================================
// Utility
// ============================================================================

static inline void tc_material_bump_version(tc_material* mat) {
    if (mat) mat->header.version++;
}

TGFX_API tc_material_handle tc_material_copy(tc_material_handle src, const char* new_uuid);

#ifdef __cplusplus
}
#endif
