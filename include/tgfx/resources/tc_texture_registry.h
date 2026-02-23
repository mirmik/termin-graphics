// tc_texture_registry.h - Global texture storage with pool + hash table
#pragma once

#include "tgfx/resources/tc_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Lifecycle (called from tc_init/tc_shutdown)
// ============================================================================

TGFX_API void tc_texture_init(void);
TGFX_API void tc_texture_shutdown(void);

// ============================================================================
// Texture operations (handle-based API)
// ============================================================================

TGFX_API tc_texture_handle tc_texture_create(const char* uuid);
TGFX_API tc_texture_handle tc_texture_find(const char* uuid);
TGFX_API tc_texture_handle tc_texture_find_by_name(const char* name);
TGFX_API tc_texture_handle tc_texture_get_or_create(const char* uuid);
TGFX_API tc_texture* tc_texture_get(tc_texture_handle h);
TGFX_API bool tc_texture_is_valid(tc_texture_handle h);
TGFX_API bool tc_texture_destroy(tc_texture_handle h);
TGFX_API bool tc_texture_contains(const char* uuid);
TGFX_API size_t tc_texture_count(void);

// ============================================================================
// Texture info for debugging/inspection
// ============================================================================

typedef struct tc_texture_info {
    tc_texture_handle handle;
    char uuid[40];
    const char* name;
    const char* source_path;
    uint32_t ref_count;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint8_t channels;
    uint8_t format;
    size_t memory_bytes;
} tc_texture_info;

TGFX_API tc_texture_info* tc_texture_get_all_info(size_t* count);

// ============================================================================
// Iteration
// ============================================================================

typedef bool (*tc_texture_iter_fn)(tc_texture_handle h, tc_texture* tex, void* user_data);
TGFX_API void tc_texture_foreach(tc_texture_iter_fn callback, void* user_data);

// ============================================================================
// Texture data helpers
// ============================================================================

TGFX_API bool tc_texture_set_data(
    tc_texture* tex,
    const void* data,
    uint32_t width,
    uint32_t height,
    uint8_t channels,
    const char* name,
    const char* source_path
);

TGFX_API void tc_texture_set_transforms(
    tc_texture* tex,
    bool flip_x,
    bool flip_y,
    bool transpose
);

static inline void tc_texture_bump_version(tc_texture* tex) {
    if (tex) tex->header.version++;
}

// ============================================================================
// Legacy API (deprecated)
// ============================================================================

TGFX_API tc_texture* tc_texture_add(const char* uuid);
TGFX_API bool tc_texture_remove(const char* uuid);

#ifdef __cplusplus
}
#endif
