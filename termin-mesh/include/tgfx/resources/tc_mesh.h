// tc_mesh.h - Mesh data structures with flexible vertex layouts
#pragma once

#include "tgfx/tgfx_api.h"
#include <tcbase/tc_resource.h>
#include <tcbase/tc_binding_types.h>
#include <tcbase/types/geom_types.h>
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
#define TC_SUBMESH_NAME_MAX 64

// ============================================================================
// Mesh data
// ============================================================================

typedef struct tc_submesh {
    uint32_t first_index;
    uint32_t index_count;
    int32_t vertex_offset;
    uint32_t material_slot;
    uint8_t draw_mode;
    char name[TC_SUBMESH_NAME_MAX];
} tc_submesh;

typedef struct tc_mesh {
    tc_resource_header header;   // common resource fields (uuid, name, version, etc.)
    void* vertices;              // raw vertex data blob
    size_t vertex_count;
    uint32_t* indices;           // indices (3 per triangle or 2 per line)
    size_t index_count;          // total indices
    tc_submesh* submeshes;       // owned draw sections/material slots
    size_t submesh_count;
    tc_vertex_layout layout;
    uint8_t draw_mode;           // tc_draw_mode (TC_DRAW_TRIANGLES or TC_DRAW_LINES)
    uint8_t _pad2[3];
} tc_mesh;

// ============================================================================
// Mesh query types
// ============================================================================

typedef struct tc_mesh_ray {
    tc_vec3f origin;
    tc_vec3f direction;
    float t_min;
    float t_max;
} tc_mesh_ray;

typedef struct tc_mesh_hit {
    float t;
    tc_vec3f position;
    tc_vec3f normal;
    tc_vec3f barycentric;
    uint32_t triangle_index;
    uint32_t indices[3];
} tc_mesh_hit;

typedef struct tc_mesh_surface_edge_hit {
    tc_vec3f point;
    uint32_t indices[2];
    float distance;
    int32_t side;
} tc_mesh_surface_edge_hit;

typedef struct tc_mesh_surface_edge_query {
    uint32_t start_triangle;
    tc_vec3f point;
    tc_vec3f normal;
    tc_vec3f up;
    tc_vec3f metric;
    bool use_direction_filter;
    tc_vec3f edge_direction;
    float max_angle_degrees;
} tc_mesh_surface_edge_query;


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

// ============================================================================
// Mesh queries
// ============================================================================

TGFX_API bool tc_mesh_get_position3f(
    const tc_mesh* mesh,
    uint32_t vertex_index,
    tc_vec3f* out_position
);

TGFX_API bool tc_mesh_get_triangle3f(
    const tc_mesh* mesh,
    uint32_t triangle_index,
    tc_vec3f* out_a,
    tc_vec3f* out_b,
    tc_vec3f* out_c
);

TGFX_API bool tc_mesh_raycast(
    const tc_mesh* mesh,
    const tc_mesh_ray* ray,
    tc_mesh_hit* out_hit
);

// Finds the nearest boundary edge of the connected surface that contains
// start_triangle.
//
// Parameters are expressed in mesh-local coordinates:
// - point: query point on/near the target surface.
// - normal: surface normal at point; used to flood-fill adjacent triangles with
//   a compatible normal and plane.
// - up: application up direction; used only to compute the diagnostic side.
//
// Distance is measured in the default unit metric (1,1,1). The returned point
// and edge vertex indices are in the original, unmodified mesh-local space.
TGFX_API bool tc_mesh_find_surface_edge(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    tc_vec3f point,
    tc_vec3f normal,
    tc_vec3f up,
    tc_mesh_surface_edge_hit* out_hit
);

// Same query as tc_mesh_find_surface_edge, but distances and angle comparisons
// are evaluated in a caller-provided diagonal metric.
//
// metric is not written back to the mesh and does not change returned
// coordinates. It is interpreted as per-axis length multipliers for measurement
// only. For example, metric=(0.5, 2, 1) means X distances count half as much and
// Y distances count twice as much while choosing the nearest edge.
//
// The returned point remains in the original unscaled mesh-local space so it can
// be transformed by the entity pose as usual.
TGFX_API bool tc_mesh_find_surface_edge_metric(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    tc_vec3f point,
    tc_vec3f normal,
    tc_vec3f up,
    tc_vec3f metric,
    tc_mesh_surface_edge_hit* out_hit
);

// General surface edge query. Use this for metric-aware aligned searches or
// when passing the query through typed API boundaries.
TGFX_API bool tc_mesh_find_surface_edge_query(
    const tc_mesh* mesh,
    const tc_mesh_surface_edge_query* query,
    tc_mesh_surface_edge_hit* out_hit
);

// Finds the nearest boundary edge of the connected surface, but only among
// edges whose direction is aligned with edge_direction within max_angle_degrees.
//
// The sign of the edge direction is ignored: an edge parallel to
// edge_direction or to -edge_direction is accepted. All input vectors are in
// mesh-local coordinates. query.metric is applied both to measured distances
// and to the direction comparison; zero metric means the default unit metric.
TGFX_API bool tc_mesh_find_surface_edge_aligned(
    const tc_mesh* mesh,
    const tc_mesh_surface_edge_query* query,
    tc_mesh_surface_edge_hit* out_hit
);

// Convenience query for callers that only have a point. It first finds the
// nearest triangle to point, derives its normal, and then runs
// tc_mesh_find_surface_edge. This is slower than passing start_triangle from a
// raycast/picking result.
TGFX_API bool tc_mesh_find_nearest_surface_edge(
    const tc_mesh* mesh,
    tc_vec3f point,
    tc_vec3f up,
    tc_mesh_surface_edge_hit* out_hit
);

// Metric-aware version of tc_mesh_find_nearest_surface_edge. The nearest
// triangle and nearest boundary edge are selected using metric-space distances,
// but the returned point remains in original mesh-local coordinates.
TGFX_API bool tc_mesh_find_nearest_surface_edge_metric(
    const tc_mesh* mesh,
    tc_vec3f point,
    tc_vec3f up,
    tc_vec3f metric,
    tc_mesh_surface_edge_hit* out_hit
);

#ifdef __cplusplus
}
#endif
