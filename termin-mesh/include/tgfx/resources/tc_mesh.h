// tc_mesh.h - Mesh data structures with flexible vertex layouts
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/resources/tc_resource.h"
#include <tcbase/tc_binding_types.h>
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
// Load callback type
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
// Mesh query types
// ============================================================================

typedef struct tc_mesh_ray {
    float origin[3];
    float direction[3];
    float t_min;
    float t_max;
} tc_mesh_ray;

typedef struct tc_mesh_hit {
    float t;
    float position[3];
    float normal[3];
    float barycentric[3];
    uint32_t triangle_index;
    uint32_t indices[3];
} tc_mesh_hit;

typedef struct tc_mesh_surface_edge_hit {
    float point[3];
    uint32_t indices[2];
    float distance;
    int32_t side;
} tc_mesh_surface_edge_hit;


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
// GPU operations callback vtable
// ============================================================================

typedef struct tc_mesh_gpu_ops {
    void     (*draw)(tc_mesh* mesh);
    uint32_t (*upload)(tc_mesh* mesh);
    void     (*delete_gpu)(tc_mesh* mesh);
} tc_mesh_gpu_ops;

TGFX_API void tc_mesh_set_gpu_ops(const tc_mesh_gpu_ops* ops);
TGFX_API const tc_mesh_gpu_ops* tc_mesh_get_gpu_ops(void);

// Convenience wrappers that dispatch through the vtable
TGFX_API void     tc_mesh_draw_gpu(tc_mesh* mesh);
TGFX_API uint32_t tc_mesh_upload_gpu(tc_mesh* mesh);
TGFX_API void     tc_mesh_delete_gpu(tc_mesh* mesh);

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
    float out_position[3]
);

TGFX_API bool tc_mesh_get_triangle3f(
    const tc_mesh* mesh,
    uint32_t triangle_index,
    float out_a[3],
    float out_b[3],
    float out_c[3]
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
    const float point[3],
    const float normal[3],
    const float up[3],
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
    const float point[3],
    const float normal[3],
    const float up[3],
    const float metric[3],
    tc_mesh_surface_edge_hit* out_hit
);

// Finds the nearest boundary edge of the connected surface, but only among
// edges whose direction is aligned with edge_direction within max_angle_degrees.
//
// The sign of the edge direction is ignored: an edge parallel to
// edge_direction or to -edge_direction is accepted. All input vectors are in
// mesh-local coordinates. Without the metric variant below, direction filtering
// and distances use the default unit metric.
TGFX_API bool tc_mesh_find_surface_edge_aligned(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    const float point[3],
    const float normal[3],
    const float up[3],
    const float edge_direction[3],
    float max_angle_degrees,
    tc_mesh_surface_edge_hit* out_hit
);

// Metric-aware aligned edge query.
//
// metric is applied both to measured distances and to the direction comparison:
// edge_direction and each candidate edge vector are first multiplied by metric
// and normalized, then compared by abs(dot). This makes the direction test
// describe alignment in the same measured geometry that is used for nearest-edge
// selection. Returned coordinates are still original mesh-local coordinates.
TGFX_API bool tc_mesh_find_surface_edge_aligned_metric(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    const float point[3],
    const float normal[3],
    const float up[3],
    const float edge_direction[3],
    float max_angle_degrees,
    const float metric[3],
    tc_mesh_surface_edge_hit* out_hit
);

// Convenience query for callers that only have a point. It first finds the
// nearest triangle to point, derives its normal, and then runs
// tc_mesh_find_surface_edge. This is slower than passing start_triangle from a
// raycast/picking result.
TGFX_API bool tc_mesh_find_nearest_surface_edge(
    const tc_mesh* mesh,
    const float point[3],
    const float up[3],
    tc_mesh_surface_edge_hit* out_hit
);

// Metric-aware version of tc_mesh_find_nearest_surface_edge. The nearest
// triangle and nearest boundary edge are selected using metric-space distances,
// but the returned point remains in original mesh-local coordinates.
TGFX_API bool tc_mesh_find_nearest_surface_edge_metric(
    const tc_mesh* mesh,
    const float point[3],
    const float up[3],
    const float metric[3],
    tc_mesh_surface_edge_hit* out_hit
);

#ifdef __cplusplus
}
#endif
