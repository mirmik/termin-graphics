// tc_mesh_registry.h - Global mesh storage with pool + hash table
#pragma once

#include "tgfx/resources/tc_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Lifecycle (called from tc_init/tc_shutdown)
// ============================================================================

TGFX_API void tc_mesh_init(void);
TGFX_API void tc_mesh_shutdown(void);

// ============================================================================
// Mesh operations (handle-based API)
// ============================================================================

TGFX_API tc_mesh_handle tc_mesh_create(const char* uuid);
TGFX_API tc_mesh_handle tc_mesh_find(const char* uuid);
TGFX_API tc_mesh_handle tc_mesh_find_by_name(const char* name);
TGFX_API tc_mesh_handle tc_mesh_get_or_create(const char* uuid);
TGFX_API tc_mesh_handle tc_mesh_declare(const char* uuid, const char* name);
TGFX_API void tc_mesh_set_load_callback(tc_mesh_handle h, tc_mesh_load_fn callback, void* user_data);
TGFX_API bool tc_mesh_is_loaded(tc_mesh_handle h);
TGFX_API bool tc_mesh_ensure_loaded(tc_mesh_handle h);
TGFX_API tc_mesh* tc_mesh_get(tc_mesh_handle h);

static inline const char* tc_mesh_uuid(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->header.uuid : NULL;
}

static inline const char* tc_mesh_name(tc_mesh_handle h) {
    tc_mesh* m = tc_mesh_get(h);
    return m ? m->header.name : NULL;
}

TGFX_API bool tc_mesh_is_valid(tc_mesh_handle h);
TGFX_API bool tc_mesh_destroy(tc_mesh_handle h);
TGFX_API bool tc_mesh_contains(const char* uuid);
TGFX_API size_t tc_mesh_count(void);

// ============================================================================
// Mesh info for debugging/inspection
// ============================================================================

typedef struct tc_mesh_info {
    tc_mesh_handle handle;
    char uuid[40];
    const char* name;
    uint32_t ref_count;
    uint32_t version;
    size_t vertex_count;
    size_t index_count;
    size_t stride;
    size_t memory_bytes;
    uint8_t is_loaded;
    uint8_t has_load_callback;
    uint8_t _pad[6];
} tc_mesh_info;

TGFX_API tc_mesh_info* tc_mesh_get_all_info(size_t* count);

// ============================================================================
// Iteration
// ============================================================================

typedef bool (*tc_mesh_iter_fn)(tc_mesh_handle h, tc_mesh* mesh, void* user_data);
TGFX_API void tc_mesh_foreach(tc_mesh_iter_fn callback, void* user_data);

// ============================================================================
// Mesh data helpers
// ============================================================================

TGFX_API bool tc_mesh_set_vertices(tc_mesh* mesh, const void* data, size_t vertex_count, const tc_vertex_layout* layout);
TGFX_API bool tc_mesh_set_indices(tc_mesh* mesh, const uint32_t* data, size_t index_count);
TGFX_API bool tc_mesh_set_data(tc_mesh* mesh, const void* vertices, size_t vertex_count, const tc_vertex_layout* layout, const uint32_t* indices, size_t index_count, const char* name);

static inline void tc_mesh_bump_version(tc_mesh* mesh) {
    if (mesh) mesh->header.version++;
}

// ============================================================================
// Mesh data export (for serialization to binary)
// ============================================================================

TGFX_API const char* tc_mesh_get_uuid_str(tc_mesh_handle h);
TGFX_API const char* tc_mesh_get_name_str(tc_mesh_handle h);
TGFX_API const void* tc_mesh_get_vertices(tc_mesh_handle h);
TGFX_API size_t tc_mesh_get_vertex_count(tc_mesh_handle h);
TGFX_API const uint32_t* tc_mesh_get_indices(tc_mesh_handle h);
TGFX_API size_t tc_mesh_get_index_count(tc_mesh_handle h);
TGFX_API tc_vertex_layout tc_mesh_get_layout(tc_mesh_handle h);
TGFX_API uint8_t tc_mesh_get_draw_mode(tc_mesh_handle h);

// ============================================================================
// Legacy API (deprecated - use handle-based API)
// ============================================================================

TGFX_API tc_mesh* tc_mesh_add(const char* uuid);
TGFX_API bool tc_mesh_remove(const char* uuid);

#ifdef __cplusplus
}
#endif
