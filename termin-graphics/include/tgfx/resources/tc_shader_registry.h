// tc_shader_registry.h - Global shader storage with pool + hash table and variant support
#pragma once

#include "tgfx/resources/tc_shader.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Lifecycle (called from tc_init/tc_shutdown)
// ============================================================================

TGFX_API void tc_shader_init(void);
TGFX_API void tc_shader_shutdown(void);

// ============================================================================
// Shader operations (handle-based API)
// ============================================================================

TGFX_API tc_shader_handle tc_shader_create(const char* uuid);
TGFX_API tc_shader_handle tc_shader_find(const char* uuid);
TGFX_API tc_shader_handle tc_shader_find_by_hash(const char* source_hash);
TGFX_API tc_shader_handle tc_shader_find_by_name(const char* name);
TGFX_API tc_shader_handle tc_shader_get_or_create(const char* uuid);
TGFX_API tc_shader* tc_shader_get(tc_shader_handle h);
TGFX_API bool tc_shader_is_valid(tc_shader_handle h);
TGFX_API bool tc_shader_destroy(tc_shader_handle h);
TGFX_API bool tc_shader_contains(const char* uuid);
TGFX_API size_t tc_shader_count(void);

// ============================================================================
// Shader source operations
// ============================================================================

TGFX_API bool tc_shader_set_sources(
    tc_shader* shader,
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name,
    const char* source_path
);

TGFX_API tc_shader_handle tc_shader_from_sources(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name,
    const char* source_path,
    const char* uuid
);

// Register a shader with process-lifetime ownership. The registry holds
// a permanent ref on the returned handle — the shader will NOT be
// destroyed when external refs reach zero, so callers don't need to
// manage add_ref / release. Use this for engine-owned shaders whose
// source is a hardcoded C literal (Shadow/Depth/Id/Normal passes, Skybox,
// Bloom, Immediate etc.) and must outlive arbitrary pass re-creations.
//
// Returns the same handle on repeated calls with identical sources
// (hash-based dedup). The registry's eternal ref is installed exactly
// once per shader; subsequent calls are cheap hash lookups.
//
// NOT for material-authored shaders — those come from `.shader` files
// and want normal ref-counted lifetime via TcShader wrappers.
TGFX_API tc_shader_handle tc_shader_register_static(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source,
    const char* name
);

// ============================================================================
// Variant support
// ============================================================================

TGFX_API void tc_shader_set_variant_info(
    tc_shader* shader,
    tc_shader_handle original,
    tc_shader_variant_op op
);

TGFX_API bool tc_shader_variant_is_stale(tc_shader_handle variant);

// ============================================================================
// Shader info for debugging/inspection
// ============================================================================

typedef struct tc_shader_info {
    tc_shader_handle handle;
    char uuid[40];
    char source_hash[TC_SHADER_HASH_LEN];
    const char* name;
    const char* source_path;
    uint32_t ref_count;
    uint32_t version;
    uint32_t features;
    size_t source_size;
    uint8_t is_variant;
    uint8_t variant_op;
    uint8_t has_geometry;
    uint8_t _pad;
} tc_shader_info;

TGFX_API tc_shader_info* tc_shader_get_all_info(size_t* count);

// ============================================================================
// Iteration
// ============================================================================

typedef bool (*tc_shader_iter_fn)(tc_shader_handle h, tc_shader* shader, void* user_data);
TGFX_API void tc_shader_foreach(tc_shader_iter_fn callback, void* user_data);

// ============================================================================
// Utility
// ============================================================================

static inline void tc_shader_bump_version(tc_shader* shader) {
    if (shader) shader->version++;
}

#ifdef __cplusplus
}
#endif
