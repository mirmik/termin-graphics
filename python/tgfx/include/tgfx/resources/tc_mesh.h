// tc_mesh.h - Mesh data structures with flexible vertex layouts
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/tc_handle.h"
#include "tgfx/resources/tc_resource.h"
#include <tgfx/tgfx_types.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Mesh handle - safe reference to mesh in pool
// ============================================================================

TC_DEFINE_HANDLE(tc_mesh_handle)

// ============================================================================
// Vertex layout types — typedef from tgfx
// ============================================================================

typedef tgfx_attrib_type tc_attrib_type;
typedef tgfx_draw_mode tc_draw_mode;
typedef tgfx_vertex_attrib tc_vertex_attrib;
typedef tgfx_vertex_layout tc_vertex_layout;

#define TC_ATTRIB_FLOAT32 TGFX_ATTRIB_FLOAT32
#define TC_ATTRIB_INT32   TGFX_ATTRIB_INT32
#define TC_ATTRIB_UINT32  TGFX_ATTRIB_UINT32
#define TC_ATTRIB_INT16   TGFX_ATTRIB_INT16
#define TC_ATTRIB_UINT16  TGFX_ATTRIB_UINT16
#define TC_ATTRIB_INT8    TGFX_ATTRIB_INT8
#define TC_ATTRIB_UINT8   TGFX_ATTRIB_UINT8

#define TC_DRAW_TRIANGLES TGFX_DRAW_TRIANGLES
#define TC_DRAW_LINES     TGFX_DRAW_LINES

#define TC_ATTRIB_NAME_MAX  TGFX_ATTRIB_NAME_MAX
#define TC_VERTEX_ATTRIBS_MAX TGFX_VERTEX_ATTRIBS_MAX

// ============================================================================
// Load callback type (legacy alias)
// ============================================================================

struct tc_mesh;
typedef bool (*tc_mesh_load_fn)(struct tc_mesh* mesh, void* user_data);

// ============================================================================
// Mesh data
// ============================================================================

typedef struct tc_mesh {
    tc_resource_header header;   // common resource fields (uuid, name, version, etc.)
    void* vertices;              // raw vertex data blob
    size_t vertex_count;
    uint32_t* indices;           // indices (3 per triangle or 2 per line)
    size_t index_count;          // total indices
    tc_vertex_layout layout;
    uint8_t draw_mode;           // tc_draw_mode (TC_DRAW_TRIANGLES or TC_DRAW_LINES)
    uint8_t _pad2[3];
} tc_mesh;


// ============================================================================
// Mesh helper functions
// ============================================================================

// Calculate total vertex data size
static inline size_t tc_mesh_vertices_size(const tc_mesh* mesh) {
    return mesh->vertex_count * mesh->layout.stride;
}

// Calculate total index data size
static inline size_t tc_mesh_indices_size(const tc_mesh* mesh) {
    return mesh->index_count * sizeof(uint32_t);
}

// Get triangle count
static inline size_t tc_mesh_triangle_count(const tc_mesh* mesh) {
    return mesh->index_count / 3;
}

// ============================================================================
// Vertex layout functions — redirect to tgfx
// ============================================================================

#define tc_attrib_type_size      tgfx_attrib_type_size
#define tc_vertex_layout_init    tgfx_vertex_layout_init
#define tc_vertex_layout_add     tgfx_vertex_layout_add
#define tc_vertex_layout_find    tgfx_vertex_layout_find

// ============================================================================
// Predefined layouts — redirect to tgfx
// ============================================================================

#define tc_vertex_layout_pos                tgfx_vertex_layout_pos
#define tc_vertex_layout_pos_normal         tgfx_vertex_layout_pos_normal
#define tc_vertex_layout_pos_normal_uv      tgfx_vertex_layout_pos_normal_uv
#define tc_vertex_layout_pos_normal_uv_tangent tgfx_vertex_layout_pos_normal_uv_tangent
#define tc_vertex_layout_pos_normal_uv_color   tgfx_vertex_layout_pos_normal_uv_color
#define tc_vertex_layout_skinned            tgfx_vertex_layout_skinned

// ============================================================================
// Reference counting
// ============================================================================

// Increment reference count
TGFX_API void tc_mesh_add_ref(tc_mesh* mesh);

// Decrement reference count. Returns true if mesh was destroyed (ref_count reached 0)
TGFX_API bool tc_mesh_release(tc_mesh* mesh);

// ============================================================================
// UUID computation
// ============================================================================

// Compute UUID from mesh data (FNV-1a hash)
// Result is written to uuid_out (must be at least 40 bytes)
TGFX_API void tc_mesh_compute_uuid(
    const void* vertices, size_t vertex_size,
    const uint32_t* indices, size_t index_count,
    char* uuid_out
);

#ifdef __cplusplus
}
#endif
