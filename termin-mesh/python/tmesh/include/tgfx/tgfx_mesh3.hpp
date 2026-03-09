#pragma once

// Pure CPU triangle mesh for geometric operations.
// No GPU layouts, no tc_mesh registry - just vertices and triangles.
// uuid field is a hint for TcMesh creation (TcMesh registers in tc_mesh with this uuid).

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

#include <tgfx/tgfx_vec3.hpp>

namespace termin {

// Pure CPU triangle mesh
class Mesh3 {
public:
    std::vector<Vec3f> vertices;
    std::vector<uint32_t> triangles;  // flat: [i0, i1, i2, i0, i1, i2, ...]
    std::vector<Vec3f> normals;       // per-vertex normals (optional)
    std::vector<Vec2f> uvs;           // per-vertex UVs (optional)
    std::vector<Vec4f> tangents;      // per-vertex tangents (optional), w = handedness
    std::string name;
    std::string uuid;  // optional UUID for caching when converting to TcMesh

    Mesh3() = default;

    Mesh3(std::vector<Vec3f> verts, std::vector<uint32_t> tris,
          std::string mesh_name = "", std::string mesh_uuid = "")
        : vertices(std::move(verts))
        , triangles(std::move(tris))
        , name(std::move(mesh_name))
        , uuid(std::move(mesh_uuid))
    {}

    // Query
    size_t vertex_count() const { return vertices.size(); }
    size_t triangle_count() const { return triangles.size() / 3; }
    bool has_normals() const { return normals.size() == vertices.size(); }
    bool has_uvs() const { return uvs.size() == vertices.size(); }
    bool has_tangents() const { return tangents.size() == vertices.size(); }
    bool is_valid() const { return !vertices.empty() && !triangles.empty(); }

    // Compute per-vertex normals from triangles
    void compute_normals() {
        if (vertices.empty() || triangles.empty()) return;

        normals.resize(vertices.size());
        for (auto& n : normals) n = {0, 0, 0};

        size_t num_tris = triangle_count();
        for (size_t t = 0; t < num_tris; t++) {
            uint32_t i0 = triangles[t * 3];
            uint32_t i1 = triangles[t * 3 + 1];
            uint32_t i2 = triangles[t * 3 + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            Vec3f e1 = vertices[i1] - vertices[i0];
            Vec3f e2 = vertices[i2] - vertices[i0];
            Vec3f face_normal = e1.cross(e2);

            normals[i0] += face_normal;
            normals[i1] += face_normal;
            normals[i2] += face_normal;
        }

        for (auto& n : normals) {
            n = n.normalized();
        }
    }

    // Compute per-vertex tangents from triangles and UVs
    // Requires normals and uvs to be present
    // Uses Lengyel's method for MikkTSpace-compatible tangents
    void compute_tangents() {
        if (vertices.empty() || triangles.empty()) return;
        if (!has_normals()) compute_normals();
        if (!has_uvs()) return;  // Can't compute tangents without UVs

        size_t vc = vertices.size();
        std::vector<Vec3f> tan1(vc, {0, 0, 0});
        std::vector<Vec3f> tan2(vc, {0, 0, 0});

        size_t num_tris = triangle_count();
        for (size_t t = 0; t < num_tris; t++) {
            uint32_t i0 = triangles[t * 3];
            uint32_t i1 = triangles[t * 3 + 1];
            uint32_t i2 = triangles[t * 3 + 2];

            if (i0 >= vc || i1 >= vc || i2 >= vc) continue;

            const Vec3f& v0 = vertices[i0];
            const Vec3f& v1 = vertices[i1];
            const Vec3f& v2 = vertices[i2];

            const Vec2f& uv0 = uvs[i0];
            const Vec2f& uv1 = uvs[i1];
            const Vec2f& uv2 = uvs[i2];

            Vec3f e1 = v1 - v0;
            Vec3f e2 = v2 - v0;

            float du1 = uv1.x - uv0.x;
            float dv1 = uv1.y - uv0.y;
            float du2 = uv2.x - uv0.x;
            float dv2 = uv2.y - uv0.y;

            float r = du1 * dv2 - du2 * dv1;
            if (std::abs(r) < 1e-8f) continue;  // Degenerate UV
            r = 1.0f / r;

            Vec3f sdir = {
                (dv2 * e1.x - dv1 * e2.x) * r,
                (dv2 * e1.y - dv1 * e2.y) * r,
                (dv2 * e1.z - dv1 * e2.z) * r
            };
            Vec3f tdir = {
                (du1 * e2.x - du2 * e1.x) * r,
                (du1 * e2.y - du2 * e1.y) * r,
                (du1 * e2.z - du2 * e1.z) * r
            };

            tan1[i0] += sdir; tan1[i1] += sdir; tan1[i2] += sdir;
            tan2[i0] += tdir; tan2[i1] += tdir; tan2[i2] += tdir;
        }

        tangents.resize(vc);
        for (size_t i = 0; i < vc; i++) {
            const Vec3f& n = normals[i];
            const Vec3f& t = tan1[i];

            // Gram-Schmidt orthogonalize
            Vec3f tangent = t - n * n.dot(t);
            float len = tangent.norm();
            if (len > 1e-6f) {
                tangent = tangent * (1.0f / len);
            } else {
                // Fallback: pick arbitrary tangent perpendicular to normal
                if (std::abs(n.x) < 0.9f) {
                    tangent = Vec3f(0, -n.z, n.y).normalized();
                } else {
                    tangent = Vec3f(-n.z, 0, n.x).normalized();
                }
            }

            // Handedness
            Vec3f cross = n.cross(t);
            float w = (cross.dot(tan2[i]) < 0.0f) ? -1.0f : 1.0f;

            tangents[i] = Vec4f(tangent.x, tangent.y, tangent.z, w);
        }
    }

    // Transformations
    void translate(float dx, float dy, float dz) {
        for (auto& v : vertices) {
            v.x += dx;
            v.y += dy;
            v.z += dz;
        }
    }

    void scale(float factor) {
        for (auto& v : vertices) {
            v.x *= factor;
            v.y *= factor;
            v.z *= factor;
        }
    }

    void scale(float sx, float sy, float sz) {
        for (auto& v : vertices) {
            v.x *= sx;
            v.y *= sy;
            v.z *= sz;
        }
    }

    // Copy
    Mesh3 copy(const std::string& new_name = "") const {
        Mesh3 result;
        result.vertices = vertices;
        result.triangles = triangles;
        result.normals = normals;
        result.uvs = uvs;
        result.tangents = tangents;
        result.name = new_name.empty() ? (name + "_copy") : new_name;
        return result;
    }
};

} // namespace termin
