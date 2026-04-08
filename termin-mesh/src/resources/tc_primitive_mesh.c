#include "tgfx/resources/tc_primitive_mesh.h"
#include <tgfx/resources/tc_mesh_registry.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Internal helpers
// ============================================================================

static tc_mesh* alloc_mesh(size_t vertex_count, size_t index_count) {
    tc_mesh* mesh = (tc_mesh*)calloc(1, sizeof(tc_mesh));
    if (!mesh) return NULL;

    mesh->layout = tc_vertex_layout_pos_normal_uv();
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;
    mesh->draw_mode = TC_DRAW_TRIANGLES;

    size_t stride = mesh->layout.stride;
    mesh->vertices = calloc(vertex_count, stride);
    mesh->indices = (uint32_t*)calloc(index_count, sizeof(uint32_t));

    if (!mesh->vertices || !mesh->indices) {
        free(mesh->vertices);
        free(mesh->indices);
        free(mesh);
        return NULL;
    }

    return mesh;
}

static void set_vertex(tc_mesh* mesh, size_t idx,
                       float px, float py, float pz,
                       float nx, float ny, float nz,
                       float u, float v) {
    size_t stride = mesh->layout.stride;
    float* ptr = (float*)((char*)mesh->vertices + idx * stride);
    ptr[0] = px;
    ptr[1] = py;
    ptr[2] = pz;
    ptr[3] = nx;
    ptr[4] = ny;
    ptr[5] = nz;
    ptr[6] = u;
    ptr[7] = v;
}

static void free_temp_mesh(tc_mesh* mesh) {
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->indices);
    free(mesh);
}

// Helper to create mesh in registry
static tc_mesh_handle create_primitive_in_registry(const char* name, tc_mesh* temp_mesh) {
    if (!temp_mesh) return tc_mesh_handle_invalid();

    tc_mesh_handle h = tc_mesh_find_by_name(name);
    if (tc_mesh_is_valid(h)) {
        free_temp_mesh(temp_mesh);

        tc_mesh* existing = tc_mesh_get(h);
        if (existing) tc_mesh_add_ref(existing);
        return h;
    }

    h = tc_mesh_create(NULL);
    if (!tc_mesh_is_valid(h)) {
        free_temp_mesh(temp_mesh);
        return tc_mesh_handle_invalid();
    }

    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh) {
        free_temp_mesh(temp_mesh);
        return tc_mesh_handle_invalid();
    }

    tc_mesh_set_data(
        mesh,
        temp_mesh->vertices,
        temp_mesh->vertex_count,
        &temp_mesh->layout,
        temp_mesh->indices,
        temp_mesh->index_count,
        name
    );
    mesh->draw_mode = temp_mesh->draw_mode;

    tc_mesh_upload_gpu(mesh);
    tc_mesh_add_ref(mesh);
    free_temp_mesh(temp_mesh);

    return h;
}

// ============================================================================
// Cube
// ============================================================================

tc_mesh* tc_primitive_cube_new(float size_x, float size_y, float size_z) {
    tc_mesh* mesh = alloc_mesh(24, 36);
    if (!mesh) return NULL;

    float hx = size_x * 0.5f;
    float hy = size_y * 0.5f;
    float hz = size_z * 0.5f;

    size_t vi = 0;
    size_t ii = 0;

    struct {
        float nx, ny, nz;
        float v[4][3];
    } faces[6] = {
        {1, 0, 0, {{hx, -hy, -hz}, {hx, hy, -hz}, {hx, hy, hz}, {hx, -hy, hz}}},
        {-1, 0, 0, {{-hx, hy, -hz}, {-hx, -hy, -hz}, {-hx, -hy, hz}, {-hx, hy, hz}}},
        {0, 1, 0, {{-hx, hy, -hz}, {-hx, hy, hz}, {hx, hy, hz}, {hx, hy, -hz}}},
        {0, -1, 0, {{-hx, -hy, hz}, {-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, -hy, hz}}},
        {0, 0, 1, {{-hx, -hy, hz}, {hx, -hy, hz}, {hx, hy, hz}, {-hx, hy, hz}}},
        {0, 0, -1, {{hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz}}},
    };

    float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (int f = 0; f < 6; f++) {
        uint32_t base = (uint32_t)vi;
        for (int c = 0; c < 4; c++) {
            set_vertex(mesh, vi++,
                       faces[f].v[c][0], faces[f].v[c][1], faces[f].v[c][2],
                       faces[f].nx, faces[f].ny, faces[f].nz,
                       uvs[c][0], uvs[c][1]);
        }
        mesh->indices[ii++] = base + 0;
        mesh->indices[ii++] = base + 1;
        mesh->indices[ii++] = base + 2;
        mesh->indices[ii++] = base + 0;
        mesh->indices[ii++] = base + 2;
        mesh->indices[ii++] = base + 3;
    }

    return mesh;
}

// ============================================================================
// Sphere
// ============================================================================

tc_mesh* tc_primitive_sphere_new(float radius, int meridians, int parallels) {
    if (meridians < 3) meridians = 3;
    if (parallels < 2) parallels = 2;

    int rings = parallels;
    int segments = meridians;

    size_t vertex_count = (rings + 1) * segments;
    size_t index_count = rings * segments * 6;

    tc_mesh* mesh = alloc_mesh(vertex_count, index_count);
    if (!mesh) return NULL;

    size_t vi = 0;
    for (int r = 0; r <= rings; r++) {
        float theta = (float)r * (float)M_PI / (float)rings;
        float sin_theta = sinf(theta);
        float cos_theta = cosf(theta);
        float v_coord = (float)r / (float)rings;

        for (int s = 0; s < segments; s++) {
            float phi = (float)s * 2.0f * (float)M_PI / (float)segments;
            float sin_phi = sinf(phi);
            float cos_phi = cosf(phi);

            float nx = sin_theta * cos_phi;
            float ny = sin_theta * sin_phi;
            float nz = cos_theta;

            float px = radius * nx;
            float py = radius * ny;
            float pz = radius * nz;

            float u_coord = (float)s / (float)segments;

            set_vertex(mesh, vi++, px, py, pz, nx, ny, nz, u_coord, v_coord);
        }
    }

    size_t ii = 0;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            int next_s = (s + 1) % segments;
            uint32_t v00 = r * segments + s;
            uint32_t v10 = (r + 1) * segments + s;
            uint32_t v01 = r * segments + next_s;
            uint32_t v11 = (r + 1) * segments + next_s;

            mesh->indices[ii++] = v00;
            mesh->indices[ii++] = v10;
            mesh->indices[ii++] = v11;

            mesh->indices[ii++] = v00;
            mesh->indices[ii++] = v11;
            mesh->indices[ii++] = v01;
        }
    }

    return mesh;
}

// ============================================================================
// Cylinder
// ============================================================================

tc_mesh* tc_primitive_cylinder_new(float radius, float height, int segments) {
    if (segments < 3) segments = 3;

    size_t side_verts = segments * 2;
    size_t cap_verts = (segments + 1) * 2;
    size_t vertex_count = side_verts + cap_verts;
    size_t index_count = segments * 6 + segments * 3 * 2;

    tc_mesh* mesh = alloc_mesh(vertex_count, index_count);
    if (!mesh) return NULL;

    float half_h = height * 0.5f;
    size_t vi = 0;
    size_t ii = 0;

    for (int ring = 0; ring < 2; ring++) {
        float y = (ring == 0) ? -half_h : half_h;
        float v_coord = (ring == 0) ? 0.0f : 1.0f;

        for (int s = 0; s < segments; s++) {
            float angle = (float)s * 2.0f * (float)M_PI / (float)segments;
            float c = cosf(angle);
            float sn = sinf(angle);
            float px = radius * c;
            float pz = radius * sn;
            float u_coord = (float)s / (float)segments;
            set_vertex(mesh, vi++, px, y, pz, c, 0, sn, u_coord, v_coord);
        }
    }

    for (int s = 0; s < segments; s++) {
        int next_s = (s + 1) % segments;
        uint32_t b0 = s;
        uint32_t b1 = next_s;
        uint32_t t0 = s + segments;
        uint32_t t1 = next_s + segments;

        mesh->indices[ii++] = b0;
        mesh->indices[ii++] = t0;
        mesh->indices[ii++] = t1;
        mesh->indices[ii++] = b0;
        mesh->indices[ii++] = t1;
        mesh->indices[ii++] = b1;
    }

    uint32_t bottom_base = (uint32_t)vi;
    for (int s = 0; s < segments; s++) {
        float angle = (float)s * 2.0f * (float)M_PI / (float)segments;
        float c = cosf(angle);
        float sn = sinf(angle);
        set_vertex(mesh, vi++, radius * c, -half_h, radius * sn, 0, -1, 0, c * 0.5f + 0.5f, sn * 0.5f + 0.5f);
    }
    uint32_t bottom_center = (uint32_t)vi;
    set_vertex(mesh, vi++, 0, -half_h, 0, 0, -1, 0, 0.5f, 0.5f);

    for (int s = 0; s < segments; s++) {
        int next_s = (s + 1) % segments;
        mesh->indices[ii++] = bottom_center;
        mesh->indices[ii++] = bottom_base + next_s;
        mesh->indices[ii++] = bottom_base + s;
    }

    uint32_t top_base = (uint32_t)vi;
    for (int s = 0; s < segments; s++) {
        float angle = (float)s * 2.0f * (float)M_PI / (float)segments;
        float c = cosf(angle);
        float sn = sinf(angle);
        set_vertex(mesh, vi++, radius * c, half_h, radius * sn, 0, 1, 0, c * 0.5f + 0.5f, sn * 0.5f + 0.5f);
    }
    uint32_t top_center = (uint32_t)vi;
    set_vertex(mesh, vi++, 0, half_h, 0, 0, 1, 0, 0.5f, 0.5f);

    for (int s = 0; s < segments; s++) {
        int next_s = (s + 1) % segments;
        mesh->indices[ii++] = top_center;
        mesh->indices[ii++] = top_base + s;
        mesh->indices[ii++] = top_base + next_s;
    }

    return mesh;
}

// ============================================================================
// Cone
// ============================================================================

tc_mesh* tc_primitive_cone_new(float radius, float height, int segments) {
    if (segments < 3) segments = 3;

    size_t vertex_count = 1 + segments + segments + 1;
    size_t index_count = segments * 3 + segments * 3;

    tc_mesh* mesh = alloc_mesh(vertex_count, index_count);
    if (!mesh) return NULL;

    float half_h = height * 0.5f;
    size_t vi = 0;
    size_t ii = 0;

    uint32_t apex_idx = (uint32_t)vi;
    set_vertex(mesh, vi++, 0, half_h, 0, 0, 1, 0, 0.5f, 1.0f);

    uint32_t base_start = (uint32_t)vi;
    float slope = radius / height;
    float ny = 1.0f / sqrtf(1.0f + slope * slope);
    float nr = slope * ny;

    for (int s = 0; s < segments; s++) {
        float angle = (float)s * 2.0f * (float)M_PI / (float)segments;
        float c = cosf(angle);
        float sn = sinf(angle);
        set_vertex(mesh, vi++, radius * c, -half_h, radius * sn,
                   nr * c, ny, nr * sn,
                   (float)s / (float)segments, 0.0f);
    }

    for (int s = 0; s < segments; s++) {
        int next_s = (s + 1) % segments;
        mesh->indices[ii++] = apex_idx;
        mesh->indices[ii++] = base_start + next_s;
        mesh->indices[ii++] = base_start + s;
    }

    uint32_t cap_start = (uint32_t)vi;
    for (int s = 0; s < segments; s++) {
        float angle = (float)s * 2.0f * (float)M_PI / (float)segments;
        float c = cosf(angle);
        float sn = sinf(angle);
        set_vertex(mesh, vi++, radius * c, -half_h, radius * sn, 0, -1, 0, c * 0.5f + 0.5f, sn * 0.5f + 0.5f);
    }
    uint32_t cap_center = (uint32_t)vi;
    set_vertex(mesh, vi++, 0, -half_h, 0, 0, -1, 0, 0.5f, 0.5f);

    for (int s = 0; s < segments; s++) {
        int next_s = (s + 1) % segments;
        mesh->indices[ii++] = cap_center;
        mesh->indices[ii++] = cap_start + next_s;
        mesh->indices[ii++] = cap_start + s;
    }

    return mesh;
}

// ============================================================================
// Plane
// ============================================================================

tc_mesh* tc_primitive_plane_new(float width, float height, int segments_w, int segments_h) {
    if (segments_w < 1) segments_w = 1;
    if (segments_h < 1) segments_h = 1;

    size_t vertex_count = (segments_w + 1) * (segments_h + 1);
    size_t index_count = segments_w * segments_h * 6;

    tc_mesh* mesh = alloc_mesh(vertex_count, index_count);
    if (!mesh) return NULL;

    size_t vi = 0;
    for (int h = 0; h <= segments_h; h++) {
        float y = ((float)h / (float)segments_h - 0.5f) * height;
        float v_coord = (float)h / (float)segments_h;

        for (int w = 0; w <= segments_w; w++) {
            float x = ((float)w / (float)segments_w - 0.5f) * width;
            float u_coord = (float)w / (float)segments_w;
            set_vertex(mesh, vi++, x, y, 0, 0, 0, 1, u_coord, v_coord);
        }
    }

    size_t ii = 0;
    for (int h = 0; h < segments_h; h++) {
        for (int w = 0; w < segments_w; w++) {
            uint32_t v0 = h * (segments_w + 1) + w;
            uint32_t v1 = v0 + 1;
            uint32_t v2 = v0 + (segments_w + 1);
            uint32_t v3 = v2 + 1;

            mesh->indices[ii++] = v0;
            mesh->indices[ii++] = v2;
            mesh->indices[ii++] = v1;
            mesh->indices[ii++] = v1;
            mesh->indices[ii++] = v2;
            mesh->indices[ii++] = v3;
        }
    }

    return mesh;
}

// ============================================================================
// Lazy Singleton Primitives (registered in mesh registry)
// ============================================================================

static tc_mesh_handle g_unit_cube = {0};
static tc_mesh_handle g_unit_sphere = {0};
static tc_mesh_handle g_unit_cylinder = {0};
static tc_mesh_handle g_unit_cone = {0};
static tc_mesh_handle g_unit_plane = {0};

tc_mesh_handle tc_primitive_unit_cube(void) {
    if (!tc_mesh_is_valid(g_unit_cube)) {
        tc_mesh* temp = tc_primitive_cube_new(1.0f, 1.0f, 1.0f);
        g_unit_cube = create_primitive_in_registry("__primitive_unit_cube", temp);
    }
    return g_unit_cube;
}

tc_mesh_handle tc_primitive_unit_sphere(void) {
    if (!tc_mesh_is_valid(g_unit_sphere)) {
        tc_mesh* temp = tc_primitive_sphere_new(0.5f, 16, 16);
        g_unit_sphere = create_primitive_in_registry("__primitive_unit_sphere", temp);
    }
    return g_unit_sphere;
}

tc_mesh_handle tc_primitive_unit_cylinder(void) {
    if (!tc_mesh_is_valid(g_unit_cylinder)) {
        tc_mesh* temp = tc_primitive_cylinder_new(0.5f, 1.0f, 16);
        g_unit_cylinder = create_primitive_in_registry("__primitive_unit_cylinder", temp);
    }
    return g_unit_cylinder;
}

tc_mesh_handle tc_primitive_unit_cone(void) {
    if (!tc_mesh_is_valid(g_unit_cone)) {
        tc_mesh* temp = tc_primitive_cone_new(0.5f, 1.0f, 16);
        g_unit_cone = create_primitive_in_registry("__primitive_unit_cone", temp);
    }
    return g_unit_cone;
}

tc_mesh_handle tc_primitive_unit_plane(void) {
    if (!tc_mesh_is_valid(g_unit_plane)) {
        tc_mesh* temp = tc_primitive_plane_new(1.0f, 1.0f, 1, 1);
        g_unit_plane = create_primitive_in_registry("__primitive_unit_plane", temp);
    }
    return g_unit_plane;
}
