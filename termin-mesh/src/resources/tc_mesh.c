// tc_mesh.c - Mesh reference counting and UUID computation
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_mesh_registry.h"
#include <tcbase/tc_log.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// GPU operations callback vtable
// ============================================================================

static const tc_mesh_gpu_ops* g_mesh_gpu_ops = NULL;

void tc_mesh_set_gpu_ops(const tc_mesh_gpu_ops* ops) {
    g_mesh_gpu_ops = ops;
}

const tc_mesh_gpu_ops* tc_mesh_get_gpu_ops(void) {
    return g_mesh_gpu_ops;
}

void tc_mesh_draw_gpu(tc_mesh* mesh) {
    if (mesh && g_mesh_gpu_ops && g_mesh_gpu_ops->draw) {
        g_mesh_gpu_ops->draw(mesh);
    }
}

uint32_t tc_mesh_upload_gpu(tc_mesh* mesh) {
    if (mesh && g_mesh_gpu_ops && g_mesh_gpu_ops->upload) {
        return g_mesh_gpu_ops->upload(mesh);
    }
    return 0;
}

void tc_mesh_delete_gpu(tc_mesh* mesh) {
    if (mesh && g_mesh_gpu_ops && g_mesh_gpu_ops->delete_gpu) {
        g_mesh_gpu_ops->delete_gpu(mesh);
    }
}

// ============================================================================
// Reference counting
// ============================================================================

void tc_mesh_add_ref(tc_mesh* mesh) {
    if (mesh) {
        mesh->header.ref_count++;
    }
}

bool tc_mesh_release(tc_mesh* mesh) {
    if (!mesh) {
        return false;
    }
    if (mesh->header.ref_count == 0) {
        tc_log(TC_LOG_WARN, "[tc_mesh_release] uuid=%s name=%s refcount already zero!",
               mesh->header.uuid, mesh->header.name ? mesh->header.name : "(null)");
        return false;
    }

    mesh->header.ref_count--;

    if (mesh->header.ref_count == 0) {
        tc_mesh_remove(mesh->header.uuid);
        return true;
    }
    return false;
}

// ============================================================================
// UUID computation (FNV-1a hash)
// ============================================================================

static uint64_t fnv1a_hash(const uint8_t* data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void tc_mesh_compute_uuid(
    const void* vertices, size_t vertex_size,
    const uint32_t* indices, size_t index_count,
    char* uuid_out
) {
    // Hash vertices
    uint64_t h1 = fnv1a_hash((const uint8_t*)vertices, vertex_size);

    // Hash indices
    uint64_t h2 = fnv1a_hash((const uint8_t*)indices, index_count * sizeof(uint32_t));

    // Combine hashes
    uint64_t combined = h1 ^ (h2 * 1099511628211ULL);

    // Format as hex string (16 chars)
    snprintf(uuid_out, 40, "%016llx", (unsigned long long)combined);
}

// ============================================================================
// Mesh queries
// ============================================================================

static bool tc_mesh_vec3_sub(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
    return true;
}

static void tc_mesh_vec3_cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float tc_mesh_vec3_dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool tc_mesh_vec3_normalize(float v[3]) {
    float len_sq = tc_mesh_vec3_dot(v, v);
    if (len_sq <= 1e-20f) {
        return false;
    }
    float inv_len = 1.0f / sqrtf(len_sq);
    v[0] *= inv_len;
    v[1] *= inv_len;
    v[2] *= inv_len;
    return true;
}

bool tc_mesh_get_position3f(
    const tc_mesh* mesh,
    uint32_t vertex_index,
    float out_position[3]
) {
    if (!mesh || !out_position || !mesh->vertices || vertex_index >= mesh->vertex_count) {
        return false;
    }

    const tc_vertex_attrib* pos = tc_vertex_layout_find(&mesh->layout, "position");
    if (!pos || pos->type != TC_ATTRIB_FLOAT32 || pos->size < 3) {
        return false;
    }

    const uint8_t* base = (const uint8_t*)mesh->vertices;
    const float* p = (const float*)(base + ((size_t)vertex_index * mesh->layout.stride) + pos->offset);
    out_position[0] = p[0];
    out_position[1] = p[1];
    out_position[2] = p[2];
    return true;
}

bool tc_mesh_get_triangle3f(
    const tc_mesh* mesh,
    uint32_t triangle_index,
    float out_a[3],
    float out_b[3],
    float out_c[3]
) {
    if (!mesh || !out_a || !out_b || !out_c || !mesh->indices) {
        return false;
    }
    if (mesh->draw_mode != TC_DRAW_TRIANGLES) {
        return false;
    }

    size_t first_index = (size_t)triangle_index * 3;
    if (first_index + 2 >= mesh->index_count) {
        return false;
    }

    uint32_t i0 = mesh->indices[first_index];
    uint32_t i1 = mesh->indices[first_index + 1];
    uint32_t i2 = mesh->indices[first_index + 2];

    return tc_mesh_get_position3f(mesh, i0, out_a) &&
           tc_mesh_get_position3f(mesh, i1, out_b) &&
           tc_mesh_get_position3f(mesh, i2, out_c);
}

bool tc_mesh_raycast(
    const tc_mesh* mesh,
    const tc_mesh_ray* ray,
    tc_mesh_hit* out_hit
) {
    if (!mesh || !ray || !out_hit) {
        return false;
    }
    if (mesh->draw_mode != TC_DRAW_TRIANGLES) {
        return false;
    }
    if (!tc_mesh_ensure_loaded_ptr((tc_mesh*)mesh)) {
        return false;
    }
    if (!mesh->indices || !mesh->vertices) {
        return false;
    }

    const tc_vertex_attrib* pos = tc_vertex_layout_find(&mesh->layout, "position");
    if (!pos || pos->type != TC_ATTRIB_FLOAT32 || pos->size < 3) {
        return false;
    }

    float dir[3] = {ray->direction[0], ray->direction[1], ray->direction[2]};
    if (!tc_mesh_vec3_normalize(dir)) {
        return false;
    }

    const float t_min = ray->t_min;
    const float t_max = ray->t_max > t_min ? ray->t_max : INFINITY;
    float best_t = t_max;
    tc_mesh_hit best_hit;
    memset(&best_hit, 0, sizeof(best_hit));
    bool found = false;

    const size_t triangle_count = mesh->index_count / 3;
    const float epsilon = 1e-7f;

    for (size_t tri = 0; tri < triangle_count; ++tri) {
        const size_t base_index = tri * 3;
        const uint32_t i0 = mesh->indices[base_index];
        const uint32_t i1 = mesh->indices[base_index + 1];
        const uint32_t i2 = mesh->indices[base_index + 2];
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count) {
            continue;
        }

        float v0[3], v1[3], v2[3];
        if (!tc_mesh_get_position3f(mesh, i0, v0) ||
            !tc_mesh_get_position3f(mesh, i1, v1) ||
            !tc_mesh_get_position3f(mesh, i2, v2)) {
            continue;
        }

        float edge1[3], edge2[3], pvec[3];
        tc_mesh_vec3_sub(v1, v0, edge1);
        tc_mesh_vec3_sub(v2, v0, edge2);
        tc_mesh_vec3_cross(dir, edge2, pvec);

        float det = tc_mesh_vec3_dot(edge1, pvec);
        if (fabsf(det) < epsilon) {
            continue;
        }

        float inv_det = 1.0f / det;
        float tvec[3];
        tc_mesh_vec3_sub(ray->origin, v0, tvec);

        float u = tc_mesh_vec3_dot(tvec, pvec) * inv_det;
        if (u < 0.0f || u > 1.0f) {
            continue;
        }

        float qvec[3];
        tc_mesh_vec3_cross(tvec, edge1, qvec);

        float v = tc_mesh_vec3_dot(dir, qvec) * inv_det;
        if (v < 0.0f || u + v > 1.0f) {
            continue;
        }

        float t = tc_mesh_vec3_dot(edge2, qvec) * inv_det;
        if (t < t_min || t > best_t) {
            continue;
        }

        float normal[3];
        tc_mesh_vec3_cross(edge1, edge2, normal);
        if (!tc_mesh_vec3_normalize(normal)) {
            continue;
        }

        best_t = t;
        found = true;
        best_hit.t = t;
        best_hit.position[0] = ray->origin[0] + dir[0] * t;
        best_hit.position[1] = ray->origin[1] + dir[1] * t;
        best_hit.position[2] = ray->origin[2] + dir[2] * t;
        best_hit.normal[0] = normal[0];
        best_hit.normal[1] = normal[1];
        best_hit.normal[2] = normal[2];
        best_hit.barycentric[0] = 1.0f - u - v;
        best_hit.barycentric[1] = u;
        best_hit.barycentric[2] = v;
        best_hit.triangle_index = (uint32_t)tri;
        best_hit.indices[0] = i0;
        best_hit.indices[1] = i1;
        best_hit.indices[2] = i2;
    }

    if (!found) {
        return false;
    }

    *out_hit = best_hit;
    return true;
}
