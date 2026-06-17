// tc_mesh.c - Mesh reference counting and UUID computation
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_mesh_registry.h"
#include <tcbase/tc_log.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <stdint.h>

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

static void tc_mesh_vec3_add(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
}

static void tc_mesh_vec3_mul(const float v[3], float k, float out[3]) {
    out[0] = v[0] * k;
    out[1] = v[1] * k;
    out[2] = v[2] * k;
}

static void tc_mesh_vec3_cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float tc_mesh_vec3_dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float tc_mesh_vec3_len_sq(const float v[3]) {
    return tc_mesh_vec3_dot(v, v);
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

static void tc_mesh_make_metric(const float metric[3], float out[3]) {
    if (!metric) {
        out[0] = 1.0f;
        out[1] = 1.0f;
        out[2] = 1.0f;
        return;
    }
    out[0] = fabsf(metric[0]) > 1e-8f ? fabsf(metric[0]) : 1e-8f;
    out[1] = fabsf(metric[1]) > 1e-8f ? fabsf(metric[1]) : 1e-8f;
    out[2] = fabsf(metric[2]) > 1e-8f ? fabsf(metric[2]) : 1e-8f;
}

static void tc_mesh_vec3_apply_metric(const float v[3], const float metric[3], float out[3]) {
    out[0] = v[0] * metric[0];
    out[1] = v[1] * metric[1];
    out[2] = v[2] * metric[2];
}

static void tc_mesh_vec3_abs_direction(const float a[3], const float b[3], float out[3]) {
    tc_mesh_vec3_sub(b, a, out);
    tc_mesh_vec3_normalize(out);
}

static void tc_mesh_closest_point_on_segment(
    const float point[3],
    const float a[3],
    const float b[3],
    float out_point[3],
    float* out_distance
) {
    float ab[3];
    float ap[3];
    tc_mesh_vec3_sub(b, a, ab);
    tc_mesh_vec3_sub(point, a, ap);
    float denom = tc_mesh_vec3_dot(ab, ab);
    float t = 0.0f;
    if (denom > 1e-12f) {
        t = tc_mesh_vec3_dot(ap, ab) / denom;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    float ab_scaled[3];
    float delta[3];
    tc_mesh_vec3_mul(ab, t, ab_scaled);
    tc_mesh_vec3_add(a, ab_scaled, out_point);
    tc_mesh_vec3_sub(out_point, point, delta);
    *out_distance = sqrtf(tc_mesh_vec3_dot(delta, delta));
}

static void tc_mesh_closest_point_on_segment_metric(
    const float point[3],
    const float a[3],
    const float b[3],
    const float metric[3],
    float out_point[3],
    float* out_distance
) {
    float point_m[3], a_m[3], b_m[3];
    tc_mesh_vec3_apply_metric(point, metric, point_m);
    tc_mesh_vec3_apply_metric(a, metric, a_m);
    tc_mesh_vec3_apply_metric(b, metric, b_m);

    float ab_m[3];
    float ap_m[3];
    tc_mesh_vec3_sub(b_m, a_m, ab_m);
    tc_mesh_vec3_sub(point_m, a_m, ap_m);
    float denom = tc_mesh_vec3_dot(ab_m, ab_m);
    float t = 0.0f;
    if (denom > 1e-12f) {
        t = tc_mesh_vec3_dot(ap_m, ab_m) / denom;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    float ab[3], ab_scaled[3], candidate_m[3], delta_m[3];
    tc_mesh_vec3_sub(b, a, ab);
    tc_mesh_vec3_mul(ab, t, ab_scaled);
    tc_mesh_vec3_add(a, ab_scaled, out_point);
    tc_mesh_vec3_apply_metric(out_point, metric, candidate_m);
    tc_mesh_vec3_sub(candidate_m, point_m, delta_m);
    *out_distance = sqrtf(tc_mesh_vec3_dot(delta_m, delta_m));
}

static void tc_mesh_closest_point_on_triangle(
    const float point[3],
    const float a[3],
    const float b[3],
    const float c[3],
    float out_point[3],
    float* out_distance
) {
    float ab[3], ac[3], ap[3];
    tc_mesh_vec3_sub(b, a, ab);
    tc_mesh_vec3_sub(c, a, ac);
    tc_mesh_vec3_sub(point, a, ap);
    float d1 = tc_mesh_vec3_dot(ab, ap);
    float d2 = tc_mesh_vec3_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        out_point[0] = a[0];
        out_point[1] = a[1];
        out_point[2] = a[2];
    } else {
        float bp[3];
        tc_mesh_vec3_sub(point, b, bp);
        float d3 = tc_mesh_vec3_dot(ab, bp);
        float d4 = tc_mesh_vec3_dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) {
            out_point[0] = b[0];
            out_point[1] = b[1];
            out_point[2] = b[2];
        } else {
            float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
                float v = d1 / (d1 - d3);
                float ab_scaled[3];
                tc_mesh_vec3_mul(ab, v, ab_scaled);
                tc_mesh_vec3_add(a, ab_scaled, out_point);
            } else {
                float cp[3];
                tc_mesh_vec3_sub(point, c, cp);
                float d5 = tc_mesh_vec3_dot(ab, cp);
                float d6 = tc_mesh_vec3_dot(ac, cp);
                if (d6 >= 0.0f && d5 <= d6) {
                    out_point[0] = c[0];
                    out_point[1] = c[1];
                    out_point[2] = c[2];
                } else {
                    float vb = d5 * d2 - d1 * d6;
                    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                        float w = d2 / (d2 - d6);
                        float ac_scaled[3];
                        tc_mesh_vec3_mul(ac, w, ac_scaled);
                        tc_mesh_vec3_add(a, ac_scaled, out_point);
                    } else {
                        float va = d3 * d6 - d5 * d4;
                        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
                            float bc[3], bc_scaled[3];
                            tc_mesh_vec3_sub(c, b, bc);
                            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                            tc_mesh_vec3_mul(bc, w, bc_scaled);
                            tc_mesh_vec3_add(b, bc_scaled, out_point);
                        } else {
                            float denom = 1.0f / (va + vb + vc);
                            float v = vb * denom;
                            float w = vc * denom;
                            float ab_scaled[3], ac_scaled[3], sum[3];
                            tc_mesh_vec3_mul(ab, v, ab_scaled);
                            tc_mesh_vec3_mul(ac, w, ac_scaled);
                            tc_mesh_vec3_add(a, ab_scaled, sum);
                            tc_mesh_vec3_add(sum, ac_scaled, out_point);
                        }
                    }
                }
            }
        }
    }

    float delta[3];
    tc_mesh_vec3_sub(out_point, point, delta);
    *out_distance = sqrtf(tc_mesh_vec3_dot(delta, delta));
}

static bool tc_mesh_triangle_indices(const tc_mesh* mesh, uint32_t tri, uint32_t out[3]) {
    if (!mesh || !mesh->indices || mesh->draw_mode != TC_DRAW_TRIANGLES) {
        return false;
    }
    size_t first = (size_t)tri * 3;
    if (first + 2 >= mesh->index_count) {
        return false;
    }
    out[0] = mesh->indices[first];
    out[1] = mesh->indices[first + 1];
    out[2] = mesh->indices[first + 2];
    return out[0] < mesh->vertex_count &&
           out[1] < mesh->vertex_count &&
           out[2] < mesh->vertex_count;
}

static bool tc_mesh_triangle_normal(const tc_mesh* mesh, uint32_t tri, float out[3]) {
    float a[3], b[3], c[3];
    if (!tc_mesh_get_triangle3f(mesh, tri, a, b, c)) {
        return false;
    }
    float ab[3], ac[3];
    tc_mesh_vec3_sub(b, a, ab);
    tc_mesh_vec3_sub(c, a, ac);
    tc_mesh_vec3_cross(ab, ac, out);
    return tc_mesh_vec3_normalize(out);
}

static bool tc_mesh_triangle_normal_metric(
    const tc_mesh* mesh,
    uint32_t tri,
    const float metric[3],
    float out[3]
) {
    float a[3], b[3], c[3];
    if (!tc_mesh_get_triangle3f(mesh, tri, a, b, c)) {
        return false;
    }
    float a_m[3], b_m[3], c_m[3];
    tc_mesh_vec3_apply_metric(a, metric, a_m);
    tc_mesh_vec3_apply_metric(b, metric, b_m);
    tc_mesh_vec3_apply_metric(c, metric, c_m);
    float ab[3], ac[3];
    tc_mesh_vec3_sub(b_m, a_m, ab);
    tc_mesh_vec3_sub(c_m, a_m, ac);
    tc_mesh_vec3_cross(ab, ac, out);
    return tc_mesh_vec3_normalize(out);
}

typedef struct tc_mesh_edge_adjacency {
    uint64_t hash;
    int64_t key[6];
    uint32_t a;
    uint32_t b;
    uint32_t tris[2];
    uint8_t count;
} tc_mesh_edge_adjacency;

static int64_t tc_mesh_quantize_coord(float value) {
    const double inv_epsilon = 100000.0;
    return (int64_t)llround((double)value * inv_epsilon);
}

static bool tc_mesh_endpoint_less(const int64_t a[3], const int64_t b[3]) {
    if (a[0] != b[0]) return a[0] < b[0];
    if (a[1] != b[1]) return a[1] < b[1];
    return a[2] < b[2];
}

static uint64_t tc_mesh_hash_edge_key(const int64_t key[6]) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < 6; ++i) {
        uint64_t value = (uint64_t)key[i];
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (uint8_t)((value >> (byte * 8)) & 0xFFu);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

static bool tc_mesh_make_geometric_edge_key(
    const tc_mesh* mesh,
    uint32_t a,
    uint32_t b,
    int64_t out_key[6],
    uint64_t* out_hash
) {
    float pa[3];
    float pb[3];
    if (!tc_mesh_get_position3f(mesh, a, pa) ||
        !tc_mesh_get_position3f(mesh, b, pb)) {
        return false;
    }

    int64_t qa[3] = {
        tc_mesh_quantize_coord(pa[0]),
        tc_mesh_quantize_coord(pa[1]),
        tc_mesh_quantize_coord(pa[2]),
    };
    int64_t qb[3] = {
        tc_mesh_quantize_coord(pb[0]),
        tc_mesh_quantize_coord(pb[1]),
        tc_mesh_quantize_coord(pb[2]),
    };

    const int64_t* first = qa;
    const int64_t* second = qb;
    if (tc_mesh_endpoint_less(qb, qa)) {
        first = qb;
        second = qa;
    }
    out_key[0] = first[0];
    out_key[1] = first[1];
    out_key[2] = first[2];
    out_key[3] = second[0];
    out_key[4] = second[1];
    out_key[5] = second[2];
    *out_hash = tc_mesh_hash_edge_key(out_key);
    return true;
}

static tc_mesh_edge_adjacency* tc_mesh_find_edge_record(
    tc_mesh_edge_adjacency* edges,
    size_t edge_count,
    const int64_t key[6],
    uint64_t hash
) {
    for (size_t i = 0; i < edge_count; ++i) {
        if (edges[i].hash == hash && memcmp(edges[i].key, key, sizeof(int64_t) * 6) == 0) {
            return &edges[i];
        }
    }
    return NULL;
}

static bool tc_mesh_add_edge_record(
    const tc_mesh* mesh,
    tc_mesh_edge_adjacency* edges,
    size_t* edge_count,
    uint32_t a,
    uint32_t b,
    uint32_t tri
) {
    int64_t key[6];
    uint64_t hash = 0;
    if (!tc_mesh_make_geometric_edge_key(mesh, a, b, key, &hash)) {
        return false;
    }
    tc_mesh_edge_adjacency* edge = tc_mesh_find_edge_record(edges, *edge_count, key, hash);
    if (!edge) {
        edge = &edges[*edge_count];
        (*edge_count)++;
        edge->hash = hash;
        memcpy(edge->key, key, sizeof(key));
        edge->a = a;
        edge->b = b;
        edge->tris[0] = tri;
        edge->tris[1] = 0;
        edge->count = 1;
        return true;
    }
    if (edge->count < 2) {
        edge->tris[edge->count] = tri;
    }
    edge->count++;
    return true;
}

static bool tc_mesh_is_boundary_edge(
    const tc_mesh* mesh,
    const tc_mesh_edge_adjacency* edges,
    size_t edge_count,
    const bool* accepted,
    uint32_t tri,
    uint32_t a,
    uint32_t b
) {
    int64_t key[6];
    uint64_t hash = 0;
    if (!tc_mesh_make_geometric_edge_key(mesh, a, b, key, &hash)) {
        return true;
    }
    const tc_mesh_edge_adjacency* edge = tc_mesh_find_edge_record(
        (tc_mesh_edge_adjacency*)edges,
        edge_count,
        key,
        hash);
    if (!edge) {
        return true;
    }
    size_t count = edge->count < 2 ? edge->count : 2;
    for (size_t i = 0; i < count; ++i) {
        uint32_t neighbor = edge->tris[i];
        if (neighbor != tri && accepted[neighbor]) {
            return false;
        }
    }
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

static bool tc_mesh_find_surface_edge_filtered(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    const float point[3],
    const float normal[3],
    const float up[3],
    const float metric[3],
    bool use_direction_filter,
    const float edge_direction[3],
    float max_angle_degrees,
    tc_mesh_surface_edge_hit* out_hit
) {
    if (!mesh || !point || !normal || !up || !out_hit) {
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

    size_t triangle_count = mesh->index_count / 3;
    if (start_triangle >= triangle_count) {
        return false;
    }

    float query_metric[3];
    tc_mesh_make_metric(metric, query_metric);

    float point_m[3];
    tc_mesh_vec3_apply_metric(point, query_metric, point_m);

    float local_up[3] = {up[0], up[1], up[2]};
    float n0[3] = {normal[0], normal[1], normal[2]};
    float local_up_m[3];
    float n0_m[3];
    tc_mesh_vec3_apply_metric(local_up, query_metric, local_up_m);
    tc_mesh_vec3_apply_metric(n0, query_metric, n0_m);
    if (!tc_mesh_vec3_normalize(local_up) || !tc_mesh_vec3_normalize(n0) ||
        !tc_mesh_vec3_normalize(local_up_m) || !tc_mesh_vec3_normalize(n0_m)) {
        return false;
    }
    float desired_edge_direction[3] = {0.0f, 0.0f, 0.0f};
    float min_edge_direction_dot = -1.0f;
    if (use_direction_filter) {
        if (!edge_direction) {
            return false;
        }
        desired_edge_direction[0] = edge_direction[0];
        desired_edge_direction[1] = edge_direction[1];
        desired_edge_direction[2] = edge_direction[2];
        tc_mesh_vec3_apply_metric(desired_edge_direction, query_metric, desired_edge_direction);
        if (!tc_mesh_vec3_normalize(desired_edge_direction)) {
            return false;
        }
        if (max_angle_degrees < 0.0f) {
            max_angle_degrees = 0.0f;
        }
        if (max_angle_degrees > 90.0f) {
            max_angle_degrees = 90.0f;
        }
        min_edge_direction_dot = cosf(max_angle_degrees * 0.01745329251994329577f);
    }

    tc_mesh_edge_adjacency* edges = (tc_mesh_edge_adjacency*)calloc(triangle_count * 3, sizeof(tc_mesh_edge_adjacency));
    float* normals = (float*)calloc(triangle_count * 3, sizeof(float));
    bool* has_normal = (bool*)calloc(triangle_count, sizeof(bool));
    bool* accepted = (bool*)calloc(triangle_count, sizeof(bool));
    uint32_t* queue = (uint32_t*)calloc(triangle_count, sizeof(uint32_t));
    if (!edges || !normals || !has_normal || !accepted || !queue) {
        free(edges);
        free(normals);
        free(has_normal);
        free(accepted);
        free(queue);
        return false;
    }

    size_t edge_count = 0;
    for (uint32_t tri = 0; tri < (uint32_t)triangle_count; ++tri) {
        uint32_t idx[3];
        if (!tc_mesh_triangle_indices(mesh, tri, idx)) {
            continue;
        }
        tc_mesh_add_edge_record(mesh, edges, &edge_count, idx[0], idx[1], tri);
        tc_mesh_add_edge_record(mesh, edges, &edge_count, idx[1], idx[2], tri);
        tc_mesh_add_edge_record(mesh, edges, &edge_count, idx[2], idx[0], tri);

        float* n = &normals[(size_t)tri * 3];
        if (tc_mesh_triangle_normal_metric(mesh, tri, query_metric, n)) {
            has_normal[tri] = true;
        }
    }

    if (!has_normal[start_triangle]) {
        free(edges);
        free(normals);
        free(has_normal);
        free(accepted);
        free(queue);
        return false;
    }

    const float normal_cos_threshold = 0.9063077870366499f;
    const float plane_distance_threshold = 0.05f;
    size_t queue_begin = 0;
    size_t queue_end = 0;
    accepted[start_triangle] = true;
    queue[queue_end++] = start_triangle;

    while (queue_begin < queue_end) {
        uint32_t tri = queue[queue_begin++];
        uint32_t idx[3];
        if (!tc_mesh_triangle_indices(mesh, tri, idx)) {
            continue;
        }

        for (int e = 0; e < 3; ++e) {
            uint32_t a = idx[e];
            uint32_t b = idx[(e + 1) % 3];
            int64_t edge_key[6];
            uint64_t edge_hash = 0;
            if (!tc_mesh_make_geometric_edge_key(mesh, a, b, edge_key, &edge_hash)) {
                continue;
            }
            tc_mesh_edge_adjacency* edge = tc_mesh_find_edge_record(edges, edge_count, edge_key, edge_hash);
            if (!edge) {
                continue;
            }
            size_t count = edge->count < 2 ? edge->count : 2;
            for (size_t i = 0; i < count; ++i) {
                uint32_t next = edge->tris[i];
                if (next == tri || accepted[next] || !has_normal[next]) {
                    continue;
                }
                float* next_normal = &normals[(size_t)next * 3];
                if (tc_mesh_vec3_dot(next_normal, n0_m) < normal_cos_threshold) {
                    continue;
                }

                uint32_t next_idx[3];
                float v0[3];
                if (!tc_mesh_triangle_indices(mesh, next, next_idx) ||
                    !tc_mesh_get_position3f(mesh, next_idx[0], v0)) {
                    continue;
                }
                float v0_m[3];
                float to_v0[3];
                tc_mesh_vec3_apply_metric(v0, query_metric, v0_m);
                tc_mesh_vec3_sub(v0_m, point_m, to_v0);
                if (fabsf(tc_mesh_vec3_dot(to_v0, n0_m)) > plane_distance_threshold) {
                    continue;
                }

                accepted[next] = true;
                queue[queue_end++] = next;
            }
        }
    }

    bool found = false;
    float best_distance = FLT_MAX;
    float best_point[3] = {0.0f, 0.0f, 0.0f};
    uint32_t best_a = 0;
    uint32_t best_b = 0;
    int32_t best_side = 0;

    float horizontal_normal[3];
    float up_part[3];
    tc_mesh_vec3_mul(local_up_m, tc_mesh_vec3_dot(n0_m, local_up_m), up_part);
    tc_mesh_vec3_sub(n0_m, up_part, horizontal_normal);
    bool has_tangent = tc_mesh_vec3_len_sq(horizontal_normal) > 1e-12f;
    float tangent[3] = {0.0f, 0.0f, 0.0f};
    if (has_tangent) {
        tc_mesh_vec3_normalize(horizontal_normal);
        tc_mesh_vec3_cross(local_up_m, horizontal_normal, tangent);
        has_tangent = tc_mesh_vec3_normalize(tangent);
    }

    for (uint32_t tri = 0; tri < (uint32_t)triangle_count; ++tri) {
        if (!accepted[tri]) {
            continue;
        }
        uint32_t idx[3];
        if (!tc_mesh_triangle_indices(mesh, tri, idx)) {
            continue;
        }
        for (int e = 0; e < 3; ++e) {
            uint32_t ia = idx[e];
            uint32_t ib = idx[(e + 1) % 3];
            if (!tc_mesh_is_boundary_edge(mesh, edges, edge_count, accepted, tri, ia, ib)) {
                continue;
            }

            float a[3], b[3];
            if (!tc_mesh_get_position3f(mesh, ia, a) ||
                !tc_mesh_get_position3f(mesh, ib, b)) {
                continue;
            }
            if (use_direction_filter) {
                float a_m[3], b_m[3], edge_dir[3];
                tc_mesh_vec3_apply_metric(a, query_metric, a_m);
                tc_mesh_vec3_apply_metric(b, query_metric, b_m);
                tc_mesh_vec3_abs_direction(a_m, b_m, edge_dir);
                if (fabsf(tc_mesh_vec3_dot(edge_dir, desired_edge_direction)) < min_edge_direction_dot) {
                    continue;
                }
            }

            float candidate[3];
            float distance = FLT_MAX;
            tc_mesh_closest_point_on_segment_metric(point, a, b, query_metric, candidate, &distance);
            if (distance < best_distance) {
                best_distance = distance;
                best_point[0] = candidate[0];
                best_point[1] = candidate[1];
                best_point[2] = candidate[2];
                best_a = ia;
                best_b = ib;
                best_side = 0;
                if (has_tangent) {
                    float candidate_m[3], delta[3];
                    tc_mesh_vec3_apply_metric(candidate, query_metric, candidate_m);
                    tc_mesh_vec3_sub(candidate_m, point_m, delta);
                    float s = tc_mesh_vec3_dot(delta, tangent);
                    if (s > 1e-5f) {
                        best_side = 1;
                    } else if (s < -1e-5f) {
                        best_side = -1;
                    }
                }
                found = true;
            }
        }
    }

    if (found) {
        out_hit->point[0] = best_point[0];
        out_hit->point[1] = best_point[1];
        out_hit->point[2] = best_point[2];
        out_hit->indices[0] = best_a;
        out_hit->indices[1] = best_b;
        out_hit->distance = best_distance;
        out_hit->side = best_side;
    }

    free(edges);
    free(normals);
    free(has_normal);
    free(accepted);
    free(queue);
    return found;
}

bool tc_mesh_find_surface_edge(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    const float point[3],
    const float normal[3],
    const float up[3],
    tc_mesh_surface_edge_hit* out_hit
) {
    const float unit_metric[3] = {1.0f, 1.0f, 1.0f};
    return tc_mesh_find_surface_edge_filtered(
        mesh,
        start_triangle,
        point,
        normal,
        up,
        unit_metric,
        false,
        NULL,
        0.0f,
        out_hit);
}

bool tc_mesh_find_surface_edge_metric(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    const float point[3],
    const float normal[3],
    const float up[3],
    const float metric[3],
    tc_mesh_surface_edge_hit* out_hit
) {
    return tc_mesh_find_surface_edge_filtered(
        mesh,
        start_triangle,
        point,
        normal,
        up,
        metric,
        false,
        NULL,
        0.0f,
        out_hit);
}

bool tc_mesh_find_surface_edge_aligned(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    const float point[3],
    const float normal[3],
    const float up[3],
    const float edge_direction[3],
    float max_angle_degrees,
    tc_mesh_surface_edge_hit* out_hit
) {
    const float unit_metric[3] = {1.0f, 1.0f, 1.0f};
    return tc_mesh_find_surface_edge_filtered(
        mesh,
        start_triangle,
        point,
        normal,
        up,
        unit_metric,
        true,
        edge_direction,
        max_angle_degrees,
        out_hit);
}

bool tc_mesh_find_surface_edge_aligned_metric(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    const float point[3],
    const float normal[3],
    const float up[3],
    const float edge_direction[3],
    float max_angle_degrees,
    const float metric[3],
    tc_mesh_surface_edge_hit* out_hit
) {
    return tc_mesh_find_surface_edge_filtered(
        mesh,
        start_triangle,
        point,
        normal,
        up,
        metric,
        true,
        edge_direction,
        max_angle_degrees,
        out_hit);
}

bool tc_mesh_find_nearest_surface_edge(
    const tc_mesh* mesh,
    const float point[3],
    const float up[3],
    tc_mesh_surface_edge_hit* out_hit
) {
    const float unit_metric[3] = {1.0f, 1.0f, 1.0f};
    return tc_mesh_find_nearest_surface_edge_metric(mesh, point, up, unit_metric, out_hit);
}

bool tc_mesh_find_nearest_surface_edge_metric(
    const tc_mesh* mesh,
    const float point[3],
    const float up[3],
    const float metric[3],
    tc_mesh_surface_edge_hit* out_hit
) {
    if (!mesh || !point || !up || !out_hit) {
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

    size_t triangle_count = mesh->index_count / 3;
    float query_metric[3];
    float point_m[3];
    tc_mesh_make_metric(metric, query_metric);
    tc_mesh_vec3_apply_metric(point, query_metric, point_m);

    bool found = false;
    uint32_t best_triangle = 0;
    float best_distance = FLT_MAX;
    float best_normal[3] = {0.0f, 0.0f, 0.0f};

    for (uint32_t tri = 0; tri < (uint32_t)triangle_count; ++tri) {
        float a[3], b[3], c[3];
        if (!tc_mesh_get_triangle3f(mesh, tri, a, b, c)) {
            continue;
        }

        float a_m[3], b_m[3], c_m[3];
        tc_mesh_vec3_apply_metric(a, query_metric, a_m);
        tc_mesh_vec3_apply_metric(b, query_metric, b_m);
        tc_mesh_vec3_apply_metric(c, query_metric, c_m);
        float closest[3];
        float distance = FLT_MAX;
        tc_mesh_closest_point_on_triangle(point_m, a_m, b_m, c_m, closest, &distance);
        if (distance >= best_distance) {
            continue;
        }

        float normal[3];
        if (!tc_mesh_triangle_normal_metric(mesh, tri, query_metric, normal)) {
            continue;
        }

        best_distance = distance;
        best_triangle = tri;
        best_normal[0] = normal[0];
        best_normal[1] = normal[1];
        best_normal[2] = normal[2];
        found = true;
    }

    if (!found) {
        return false;
    }

    return tc_mesh_find_surface_edge_metric(mesh, best_triangle, point, best_normal, up, query_metric, out_hit);
}
