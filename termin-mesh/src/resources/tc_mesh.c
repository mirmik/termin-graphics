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

static tc_vec3f tc_mesh_vec3f_make(float x, float y, float z) {
    tc_vec3f out;
    out.x = x;
    out.y = y;
    out.z = z;
    return out;
}

static tc_vec3f tc_mesh_vec3f_zero(void) {
    return tc_mesh_vec3f_make(0.0f, 0.0f, 0.0f);
}

static tc_vec3f tc_mesh_vec3f_one(void) {
    return tc_mesh_vec3f_make(1.0f, 1.0f, 1.0f);
}

static tc_vec3f tc_mesh_vec3f_add(tc_vec3f a, tc_vec3f b) {
    return tc_mesh_vec3f_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

static tc_vec3f tc_mesh_vec3f_sub(tc_vec3f a, tc_vec3f b) {
    return tc_mesh_vec3f_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

static tc_vec3f tc_mesh_vec3f_mul(tc_vec3f v, float k) {
    return tc_mesh_vec3f_make(v.x * k, v.y * k, v.z * k);
}

static tc_vec3f tc_mesh_vec3f_cross(tc_vec3f a, tc_vec3f b) {
    return tc_mesh_vec3f_make(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

static float tc_mesh_vec3f_dot(tc_vec3f a, tc_vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float tc_mesh_vec3f_len_sq(tc_vec3f v) {
    return tc_mesh_vec3f_dot(v, v);
}

static bool tc_mesh_vec3f_normalize(tc_vec3f* v) {
    float len_sq = tc_mesh_vec3f_len_sq(*v);
    if (len_sq <= 1e-20f) {
        return false;
    }
    float inv_len = 1.0f / sqrtf(len_sq);
    v->x *= inv_len;
    v->y *= inv_len;
    v->z *= inv_len;
    return true;
}

static bool tc_mesh_vec3f_is_zero_metric(tc_vec3f metric) {
    return fabsf(metric.x) <= 1e-8f &&
           fabsf(metric.y) <= 1e-8f &&
           fabsf(metric.z) <= 1e-8f;
}

static tc_vec3f tc_mesh_make_metric(tc_vec3f metric) {
    if (tc_mesh_vec3f_is_zero_metric(metric)) {
        return tc_mesh_vec3f_one();
    }
    return tc_mesh_vec3f_make(
        fabsf(metric.x) > 1e-8f ? fabsf(metric.x) : 1e-8f,
        fabsf(metric.y) > 1e-8f ? fabsf(metric.y) : 1e-8f,
        fabsf(metric.z) > 1e-8f ? fabsf(metric.z) : 1e-8f);
}

static tc_vec3f tc_mesh_vec3f_apply_metric(tc_vec3f v, tc_vec3f metric) {
    return tc_mesh_vec3f_make(v.x * metric.x, v.y * metric.y, v.z * metric.z);
}

static tc_vec3f tc_mesh_vec3f_abs_direction(tc_vec3f a, tc_vec3f b) {
    tc_vec3f out = tc_mesh_vec3f_sub(b, a);
    tc_mesh_vec3f_normalize(&out);
    return out;
}

static void tc_mesh_closest_point_on_segment(
    tc_vec3f point,
    tc_vec3f a,
    tc_vec3f b,
    tc_vec3f* out_point,
    float* out_distance
) {
    tc_vec3f ab = tc_mesh_vec3f_sub(b, a);
    tc_vec3f ap = tc_mesh_vec3f_sub(point, a);
    float denom = tc_mesh_vec3f_dot(ab, ab);
    float t = 0.0f;
    if (denom > 1e-12f) {
        t = tc_mesh_vec3f_dot(ap, ab) / denom;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    *out_point = tc_mesh_vec3f_add(a, tc_mesh_vec3f_mul(ab, t));
    tc_vec3f delta = tc_mesh_vec3f_sub(*out_point, point);
    *out_distance = sqrtf(tc_mesh_vec3f_dot(delta, delta));
}

static void tc_mesh_closest_point_on_segment_metric(
    tc_vec3f point,
    tc_vec3f a,
    tc_vec3f b,
    tc_vec3f metric,
    tc_vec3f* out_point,
    float* out_distance
) {
    tc_vec3f point_m = tc_mesh_vec3f_apply_metric(point, metric);
    tc_vec3f a_m = tc_mesh_vec3f_apply_metric(a, metric);
    tc_vec3f b_m = tc_mesh_vec3f_apply_metric(b, metric);
    tc_vec3f ab_m = tc_mesh_vec3f_sub(b_m, a_m);
    tc_vec3f ap_m = tc_mesh_vec3f_sub(point_m, a_m);
    float denom = tc_mesh_vec3f_dot(ab_m, ab_m);
    float t = 0.0f;
    if (denom > 1e-12f) {
        t = tc_mesh_vec3f_dot(ap_m, ab_m) / denom;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    tc_vec3f ab = tc_mesh_vec3f_sub(b, a);
    *out_point = tc_mesh_vec3f_add(a, tc_mesh_vec3f_mul(ab, t));
    tc_vec3f candidate_m = tc_mesh_vec3f_apply_metric(*out_point, metric);
    tc_vec3f delta_m = tc_mesh_vec3f_sub(candidate_m, point_m);
    *out_distance = sqrtf(tc_mesh_vec3f_dot(delta_m, delta_m));
}

static void tc_mesh_closest_point_on_triangle(
    tc_vec3f point,
    tc_vec3f a,
    tc_vec3f b,
    tc_vec3f c,
    tc_vec3f* out_point,
    float* out_distance
) {
    tc_vec3f ab = tc_mesh_vec3f_sub(b, a);
    tc_vec3f ac = tc_mesh_vec3f_sub(c, a);
    tc_vec3f ap = tc_mesh_vec3f_sub(point, a);
    float d1 = tc_mesh_vec3f_dot(ab, ap);
    float d2 = tc_mesh_vec3f_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        *out_point = a;
    } else {
        tc_vec3f bp = tc_mesh_vec3f_sub(point, b);
        float d3 = tc_mesh_vec3f_dot(ab, bp);
        float d4 = tc_mesh_vec3f_dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) {
            *out_point = b;
        } else {
            float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
                float v = d1 / (d1 - d3);
                *out_point = tc_mesh_vec3f_add(a, tc_mesh_vec3f_mul(ab, v));
            } else {
                tc_vec3f cp = tc_mesh_vec3f_sub(point, c);
                float d5 = tc_mesh_vec3f_dot(ab, cp);
                float d6 = tc_mesh_vec3f_dot(ac, cp);
                if (d6 >= 0.0f && d5 <= d6) {
                    *out_point = c;
                } else {
                    float vb = d5 * d2 - d1 * d6;
                    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                        float w = d2 / (d2 - d6);
                        *out_point = tc_mesh_vec3f_add(a, tc_mesh_vec3f_mul(ac, w));
                    } else {
                        float va = d3 * d6 - d5 * d4;
                        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
                            tc_vec3f bc = tc_mesh_vec3f_sub(c, b);
                            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                            *out_point = tc_mesh_vec3f_add(b, tc_mesh_vec3f_mul(bc, w));
                        } else {
                            float denom = 1.0f / (va + vb + vc);
                            float v = vb * denom;
                            float w = vc * denom;
                            tc_vec3f sum = tc_mesh_vec3f_add(a, tc_mesh_vec3f_mul(ab, v));
                            *out_point = tc_mesh_vec3f_add(sum, tc_mesh_vec3f_mul(ac, w));
                        }
                    }
                }
            }
        }
    }

    tc_vec3f delta = tc_mesh_vec3f_sub(*out_point, point);
    *out_distance = sqrtf(tc_mesh_vec3f_dot(delta, delta));
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

static bool tc_mesh_triangle_normal(const tc_mesh* mesh, uint32_t tri, tc_vec3f* out) {
    tc_vec3f a;
    tc_vec3f b;
    tc_vec3f c;
    if (!tc_mesh_get_triangle3f(mesh, tri, &a, &b, &c)) {
        return false;
    }
    tc_vec3f ab = tc_mesh_vec3f_sub(b, a);
    tc_vec3f ac = tc_mesh_vec3f_sub(c, a);
    *out = tc_mesh_vec3f_cross(ab, ac);
    return tc_mesh_vec3f_normalize(out);
}

static bool tc_mesh_triangle_normal_metric(
    const tc_mesh* mesh,
    uint32_t tri,
    tc_vec3f metric,
    tc_vec3f* out
) {
    tc_vec3f a;
    tc_vec3f b;
    tc_vec3f c;
    if (!tc_mesh_get_triangle3f(mesh, tri, &a, &b, &c)) {
        return false;
    }
    tc_vec3f a_m = tc_mesh_vec3f_apply_metric(a, metric);
    tc_vec3f b_m = tc_mesh_vec3f_apply_metric(b, metric);
    tc_vec3f c_m = tc_mesh_vec3f_apply_metric(c, metric);
    tc_vec3f ab = tc_mesh_vec3f_sub(b_m, a_m);
    tc_vec3f ac = tc_mesh_vec3f_sub(c_m, a_m);
    *out = tc_mesh_vec3f_cross(ab, ac);
    return tc_mesh_vec3f_normalize(out);
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
    tc_vec3f pa;
    tc_vec3f pb;
    if (!tc_mesh_get_position3f(mesh, a, &pa) ||
        !tc_mesh_get_position3f(mesh, b, &pb)) {
        return false;
    }

    int64_t qa[3] = {
        tc_mesh_quantize_coord(pa.x),
        tc_mesh_quantize_coord(pa.y),
        tc_mesh_quantize_coord(pa.z),
    };
    int64_t qb[3] = {
        tc_mesh_quantize_coord(pb.x),
        tc_mesh_quantize_coord(pb.y),
        tc_mesh_quantize_coord(pb.z),
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
    tc_vec3f* out_position
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
    *out_position = tc_mesh_vec3f_make(p[0], p[1], p[2]);
    return true;
}

bool tc_mesh_get_triangle3f(
    const tc_mesh* mesh,
    uint32_t triangle_index,
    tc_vec3f* out_a,
    tc_vec3f* out_b,
    tc_vec3f* out_c
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

    tc_vec3f dir = ray->direction;
    if (!tc_mesh_vec3f_normalize(&dir)) {
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

        tc_vec3f v0;
        tc_vec3f v1;
        tc_vec3f v2;
        if (!tc_mesh_get_position3f(mesh, i0, &v0) ||
            !tc_mesh_get_position3f(mesh, i1, &v1) ||
            !tc_mesh_get_position3f(mesh, i2, &v2)) {
            continue;
        }

        tc_vec3f edge1 = tc_mesh_vec3f_sub(v1, v0);
        tc_vec3f edge2 = tc_mesh_vec3f_sub(v2, v0);
        tc_vec3f pvec = tc_mesh_vec3f_cross(dir, edge2);

        float det = tc_mesh_vec3f_dot(edge1, pvec);
        if (fabsf(det) < epsilon) {
            continue;
        }

        float inv_det = 1.0f / det;
        tc_vec3f tvec = tc_mesh_vec3f_sub(ray->origin, v0);

        float u = tc_mesh_vec3f_dot(tvec, pvec) * inv_det;
        if (u < 0.0f || u > 1.0f) {
            continue;
        }

        tc_vec3f qvec = tc_mesh_vec3f_cross(tvec, edge1);

        float v = tc_mesh_vec3f_dot(dir, qvec) * inv_det;
        if (v < 0.0f || u + v > 1.0f) {
            continue;
        }

        float t = tc_mesh_vec3f_dot(edge2, qvec) * inv_det;
        if (t < t_min || t > best_t) {
            continue;
        }

        tc_vec3f normal = tc_mesh_vec3f_cross(edge1, edge2);
        if (!tc_mesh_vec3f_normalize(&normal)) {
            continue;
        }

        best_t = t;
        found = true;
        best_hit.t = t;
        best_hit.position = tc_mesh_vec3f_add(ray->origin, tc_mesh_vec3f_mul(dir, t));
        best_hit.normal = normal;
        best_hit.barycentric = tc_mesh_vec3f_make(1.0f - u - v, u, v);
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
    const tc_mesh_surface_edge_query* query,
    tc_mesh_surface_edge_hit* out_hit
) {
    if (!mesh || !query || !out_hit) {
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
    if (query->start_triangle >= triangle_count) {
        return false;
    }

    tc_vec3f query_metric = tc_mesh_make_metric(query->metric);
    tc_vec3f point_m = tc_mesh_vec3f_apply_metric(query->point, query_metric);

    tc_vec3f local_up = query->up;
    tc_vec3f n0 = query->normal;
    tc_vec3f local_up_m = tc_mesh_vec3f_apply_metric(local_up, query_metric);
    tc_vec3f n0_m = tc_mesh_vec3f_apply_metric(n0, query_metric);
    if (!tc_mesh_vec3f_normalize(&local_up) || !tc_mesh_vec3f_normalize(&n0) ||
        !tc_mesh_vec3f_normalize(&local_up_m) || !tc_mesh_vec3f_normalize(&n0_m)) {
        return false;
    }
    tc_vec3f desired_edge_direction = tc_mesh_vec3f_zero();
    float min_edge_direction_dot = -1.0f;
    if (query->use_direction_filter) {
        desired_edge_direction = tc_mesh_vec3f_apply_metric(query->edge_direction, query_metric);
        if (!tc_mesh_vec3f_normalize(&desired_edge_direction)) {
            return false;
        }
        float max_angle_degrees = query->max_angle_degrees;
        if (max_angle_degrees < 0.0f) {
            max_angle_degrees = 0.0f;
        }
        if (max_angle_degrees > 90.0f) {
            max_angle_degrees = 90.0f;
        }
        min_edge_direction_dot = cosf(max_angle_degrees * 0.01745329251994329577f);
    }

    tc_mesh_edge_adjacency* edges = (tc_mesh_edge_adjacency*)calloc(triangle_count * 3, sizeof(tc_mesh_edge_adjacency));
    tc_vec3f* normals = (tc_vec3f*)calloc(triangle_count, sizeof(tc_vec3f));
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

        tc_vec3f* n = &normals[tri];
        if (tc_mesh_triangle_normal_metric(mesh, tri, query_metric, n)) {
            has_normal[tri] = true;
        }
    }

    if (!has_normal[query->start_triangle]) {
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
    accepted[query->start_triangle] = true;
    queue[queue_end++] = query->start_triangle;

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
                tc_vec3f next_normal = normals[next];
                if (tc_mesh_vec3f_dot(next_normal, n0_m) < normal_cos_threshold) {
                    continue;
                }

                uint32_t next_idx[3];
                tc_vec3f v0;
                if (!tc_mesh_triangle_indices(mesh, next, next_idx) ||
                    !tc_mesh_get_position3f(mesh, next_idx[0], &v0)) {
                    continue;
                }
                tc_vec3f v0_m = tc_mesh_vec3f_apply_metric(v0, query_metric);
                tc_vec3f to_v0 = tc_mesh_vec3f_sub(v0_m, point_m);
                if (fabsf(tc_mesh_vec3f_dot(to_v0, n0_m)) > plane_distance_threshold) {
                    continue;
                }

                accepted[next] = true;
                queue[queue_end++] = next;
            }
        }
    }

    bool found = false;
    float best_distance = FLT_MAX;
    tc_vec3f best_point = tc_mesh_vec3f_zero();
    uint32_t best_a = 0;
    uint32_t best_b = 0;
    int32_t best_side = 0;

    tc_vec3f up_part = tc_mesh_vec3f_mul(local_up_m, tc_mesh_vec3f_dot(n0_m, local_up_m));
    tc_vec3f horizontal_normal = tc_mesh_vec3f_sub(n0_m, up_part);
    bool has_tangent = tc_mesh_vec3f_len_sq(horizontal_normal) > 1e-12f;
    tc_vec3f tangent = tc_mesh_vec3f_zero();
    if (has_tangent) {
        tc_mesh_vec3f_normalize(&horizontal_normal);
        tangent = tc_mesh_vec3f_cross(local_up_m, horizontal_normal);
        has_tangent = tc_mesh_vec3f_normalize(&tangent);
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

            tc_vec3f a;
            tc_vec3f b;
            if (!tc_mesh_get_position3f(mesh, ia, &a) ||
                !tc_mesh_get_position3f(mesh, ib, &b)) {
                continue;
            }
            if (query->use_direction_filter) {
                tc_vec3f a_m = tc_mesh_vec3f_apply_metric(a, query_metric);
                tc_vec3f b_m = tc_mesh_vec3f_apply_metric(b, query_metric);
                tc_vec3f edge_dir = tc_mesh_vec3f_abs_direction(a_m, b_m);
                if (fabsf(tc_mesh_vec3f_dot(edge_dir, desired_edge_direction)) < min_edge_direction_dot) {
                    continue;
                }
            }

            tc_vec3f candidate;
            float distance = FLT_MAX;
            tc_mesh_closest_point_on_segment_metric(
                query->point,
                a,
                b,
                query_metric,
                &candidate,
                &distance);
            if (distance < best_distance) {
                best_distance = distance;
                best_point = candidate;
                best_a = ia;
                best_b = ib;
                best_side = 0;
                if (has_tangent) {
                    tc_vec3f candidate_m = tc_mesh_vec3f_apply_metric(candidate, query_metric);
                    tc_vec3f delta = tc_mesh_vec3f_sub(candidate_m, point_m);
                    float s = tc_mesh_vec3f_dot(delta, tangent);
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
        out_hit->point = best_point;
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
    tc_vec3f point,
    tc_vec3f normal,
    tc_vec3f up,
    tc_mesh_surface_edge_hit* out_hit
) {
    tc_mesh_surface_edge_query query = {0};
    query.start_triangle = start_triangle;
    query.point = point;
    query.normal = normal;
    query.up = up;
    query.metric = tc_mesh_vec3f_one();
    return tc_mesh_find_surface_edge_query(mesh, &query, out_hit);
}

bool tc_mesh_find_surface_edge_metric(
    const tc_mesh* mesh,
    uint32_t start_triangle,
    tc_vec3f point,
    tc_vec3f normal,
    tc_vec3f up,
    tc_vec3f metric,
    tc_mesh_surface_edge_hit* out_hit
) {
    tc_mesh_surface_edge_query query = {0};
    query.start_triangle = start_triangle;
    query.point = point;
    query.normal = normal;
    query.up = up;
    query.metric = metric;
    return tc_mesh_find_surface_edge_query(mesh, &query, out_hit);
}

bool tc_mesh_find_surface_edge_query(
    const tc_mesh* mesh,
    const tc_mesh_surface_edge_query* query,
    tc_mesh_surface_edge_hit* out_hit
) {
    return tc_mesh_find_surface_edge_filtered(mesh, query, out_hit);
}

bool tc_mesh_find_surface_edge_aligned(
    const tc_mesh* mesh,
    const tc_mesh_surface_edge_query* query,
    tc_mesh_surface_edge_hit* out_hit
) {
    if (!query) {
        return false;
    }
    tc_mesh_surface_edge_query aligned_query = *query;
    aligned_query.use_direction_filter = true;
    return tc_mesh_find_surface_edge_query(mesh, &aligned_query, out_hit);
}

bool tc_mesh_find_nearest_surface_edge(
    const tc_mesh* mesh,
    tc_vec3f point,
    tc_vec3f up,
    tc_mesh_surface_edge_hit* out_hit
) {
    return tc_mesh_find_nearest_surface_edge_metric(mesh, point, up, tc_mesh_vec3f_one(), out_hit);
}

bool tc_mesh_find_nearest_surface_edge_metric(
    const tc_mesh* mesh,
    tc_vec3f point,
    tc_vec3f up,
    tc_vec3f metric,
    tc_mesh_surface_edge_hit* out_hit
) {
    if (!mesh || !out_hit) {
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
    tc_vec3f query_metric = tc_mesh_make_metric(metric);
    tc_vec3f point_m = tc_mesh_vec3f_apply_metric(point, query_metric);

    bool found = false;
    uint32_t best_triangle = 0;
    float best_distance = FLT_MAX;
    tc_vec3f best_normal = tc_mesh_vec3f_zero();

    for (uint32_t tri = 0; tri < (uint32_t)triangle_count; ++tri) {
        tc_vec3f a;
        tc_vec3f b;
        tc_vec3f c;
        if (!tc_mesh_get_triangle3f(mesh, tri, &a, &b, &c)) {
            continue;
        }

        tc_vec3f a_m = tc_mesh_vec3f_apply_metric(a, query_metric);
        tc_vec3f b_m = tc_mesh_vec3f_apply_metric(b, query_metric);
        tc_vec3f c_m = tc_mesh_vec3f_apply_metric(c, query_metric);
        tc_vec3f closest;
        float distance = FLT_MAX;
        tc_mesh_closest_point_on_triangle(point_m, a_m, b_m, c_m, &closest, &distance);
        if (distance >= best_distance) {
            continue;
        }

        tc_vec3f normal;
        if (!tc_mesh_triangle_normal_metric(mesh, tri, query_metric, &normal)) {
            continue;
        }

        best_distance = distance;
        best_triangle = tri;
        best_normal = normal;
        found = true;
    }

    if (!found) {
        return false;
    }

    return tc_mesh_find_surface_edge_metric(mesh, best_triangle, point, best_normal, up, query_metric, out_hit);
}
