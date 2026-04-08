#ifndef TC_PRIMITIVE_MESH_H
#define TC_PRIMITIVE_MESH_H

#include "tgfx/resources/tc_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Primitive Mesh Generation
// ============================================================================
// These functions create NEW meshes each time - caller must manage them.
// For shared primitives, use the unit_* functions below.

// Generate a cube mesh (caller owns the returned mesh)
TGFX_API tc_mesh* tc_primitive_cube_new(float size_x, float size_y, float size_z);

// Generate a UV sphere mesh (caller owns the returned mesh)
TGFX_API tc_mesh* tc_primitive_sphere_new(float radius, int meridians, int parallels);

// Generate a cylinder mesh with caps (caller owns the returned mesh)
TGFX_API tc_mesh* tc_primitive_cylinder_new(float radius, float height, int segments);

// Generate a cone mesh (caller owns the returned mesh)
TGFX_API tc_mesh* tc_primitive_cone_new(float radius, float height, int segments);

// Generate a plane mesh in XY plane (caller owns the returned mesh)
TGFX_API tc_mesh* tc_primitive_plane_new(float width, float height, int segments_w, int segments_h);

// ============================================================================
// Lazy Singleton Primitives (registered in mesh registry)
// ============================================================================
// These return handles to shared meshes in the registry.
// The meshes are created on first call and cached.
// Do NOT destroy these handles - they are shared.

// Get shared unit cube (1x1x1, centered at origin)
TGFX_API tc_mesh_handle tc_primitive_unit_cube(void);

// Get shared unit sphere (radius=1, 16x16 segments)
TGFX_API tc_mesh_handle tc_primitive_unit_sphere(void);

// Get shared unit cylinder (radius=1, height=1, 16 segments)
TGFX_API tc_mesh_handle tc_primitive_unit_cylinder(void);

// Get shared unit cone (radius=1, height=1, 16 segments)
TGFX_API tc_mesh_handle tc_primitive_unit_cone(void);

// Get shared unit plane (1x1 in XY plane)
TGFX_API tc_mesh_handle tc_primitive_unit_plane(void);

// ============================================================================
// Deprecated API (for backward compatibility)
// ============================================================================

// Deprecated: use tc_primitive_cube_new instead
static inline tc_mesh* tc_primitive_cube(float sx, float sy, float sz) {
    return tc_primitive_cube_new(sx, sy, sz);
}

// Deprecated: use tc_primitive_sphere_new instead
static inline tc_mesh* tc_primitive_sphere(float r, int m, int p) {
    return tc_primitive_sphere_new(r, m, p);
}

// Deprecated: use tc_primitive_cylinder_new instead
static inline tc_mesh* tc_primitive_cylinder(float r, float h, int s) {
    return tc_primitive_cylinder_new(r, h, s);
}

// Deprecated: use tc_primitive_cone_new instead
static inline tc_mesh* tc_primitive_cone(float r, float h, int s) {
    return tc_primitive_cone_new(r, h, s);
}

// Deprecated: use tc_primitive_plane_new instead
static inline tc_mesh* tc_primitive_plane(float w, float h, int sw, int sh) {
    return tc_primitive_plane_new(w, h, sw, sh);
}

// No longer needed - primitives use mesh registry
static inline void tc_primitive_cleanup(void) {}

#ifdef __cplusplus
}
#endif

#endif // TC_PRIMITIVE_MESH_H
