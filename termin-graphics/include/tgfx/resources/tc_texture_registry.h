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
// Destroy hooks
// ============================================================================
//
// A destroy-hook is fired inside `tc_texture_destroy()` before the texture's
// CPU data and pool slot are released. The hook receives the tc_texture's
// `header.pool_index` — GPU-side caches indexed by pool_index (e.g.
// VulkanRenderDevice::tc_texture_cache_) use this to drop their entries
// and destroy the underlying GPU objects before the pool slot is recycled.
//
// Hooks must be cheap and non-blocking: destroy happens inside the user's
// thread and must not leave the registry in an inconsistent state. Up to
// TC_MAX_TEXTURE_DESTROY_HOOKS subscribers are supported; exceeding this
// logs an error and silently drops the registration.

#define TC_MAX_TEXTURE_DESTROY_HOOKS 16

typedef void (*tc_texture_destroy_hook_fn)(uint32_t pool_index, void* user_data);

TGFX_API void tc_texture_registry_add_destroy_hook(
    tc_texture_destroy_hook_fn cb, void* user_data);
TGFX_API void tc_texture_registry_remove_destroy_hook(
    tc_texture_destroy_hook_fn cb, void* user_data);

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
