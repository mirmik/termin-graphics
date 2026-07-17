#include <tgfx2/immediate_renderer.hpp>

#include <tgfx2/builtin_shader_sources.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/vertex_layout.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

extern "C" {
#include "tc_profiler.h"
}

#include <cmath>
#include <cstring>
#include <algorithm>

// MSVC doesn't define M_PI by default
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace termin {

namespace {

constexpr const char* IMMEDIATE_ENGINE_SHADER_UUID = "termin-engine-immediate";

struct ImmediatePushData {
    float u_vp[16];
};
static_assert(sizeof(ImmediatePushData) == 64,
              "ImmediatePushData must be 64 bytes (mat4)");

} // anonymous namespace


void ImmediateRenderer::begin() {
    line_vertices.clear();
    tri_vertices.clear();
    line_vertices_depth.clear();
    tri_vertices_depth.clear();
}

void ImmediateRenderer::_add_vertex(std::vector<float>& buffer, const Vec3& pos, const Color4& color) {
    buffer.push_back(static_cast<float>(pos.x));
    buffer.push_back(static_cast<float>(pos.y));
    buffer.push_back(static_cast<float>(pos.z));
    buffer.push_back(color.r);
    buffer.push_back(color.g);
    buffer.push_back(color.b);
    buffer.push_back(color.a);
}

std::pair<Vec3, Vec3> ImmediateRenderer::_build_basis(const Vec3& axis) {
    Vec3 up{0.0, 0.0, 1.0};
    if (std::abs(axis.dot(up)) > 0.99) {
        up = Vec3{0.0, 1.0, 0.0};
    }
    Vec3 tangent = axis.cross(up).normalized();
    Vec3 bitangent = axis.cross(tangent);
    return {tangent, bitangent};
}

// ============================================================
// Basic primitives
// ============================================================

void ImmediateRenderer::line(const Vec3& start, const Vec3& end, const Color4& color, bool depth_test) {
    auto& buf = depth_test ? line_vertices_depth : line_vertices;
    _add_vertex(buf, start, color);
    _add_vertex(buf, end, color);
}

void ImmediateRenderer::triangle(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Color4& color, bool depth_test) {
    auto& buf = depth_test ? tri_vertices_depth : tri_vertices;
    _add_vertex(buf, p0, color);
    _add_vertex(buf, p1, color);
    _add_vertex(buf, p2, color);
}

void ImmediateRenderer::quad(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, const Color4& color, bool depth_test) {
    triangle(p0, p1, p2, color, depth_test);
    triangle(p0, p2, p3, color, depth_test);
}

void ImmediateRenderer::triangles(
    const float* vertices, size_t vertex_count,
    const uint32_t* indices, size_t triangle_count,
    const float* colors,
    bool depth_test
) {
    auto& buf = depth_test ? tri_vertices_depth : tri_vertices;

    // Reserve space for efficiency
    buf.reserve(buf.size() + triangle_count * 3 * 7);

    for (size_t t = 0; t < triangle_count; ++t) {
        for (size_t v = 0; v < 3; ++v) {
            uint32_t idx = indices[t * 3 + v];
            // Position (3 floats)
            buf.push_back(vertices[idx * 3 + 0]);
            buf.push_back(vertices[idx * 3 + 1]);
            buf.push_back(vertices[idx * 3 + 2]);
            // Color (4 floats)
            buf.push_back(colors[idx * 4 + 0]);
            buf.push_back(colors[idx * 4 + 1]);
            buf.push_back(colors[idx * 4 + 2]);
            buf.push_back(colors[idx * 4 + 3]);
        }
    }
}

void ImmediateRenderer::triangles(
    const float* vertices, size_t vertex_count,
    const uint32_t* indices, size_t triangle_count,
    const Color4& color,
    bool depth_test
) {
    auto& buf = depth_test ? tri_vertices_depth : tri_vertices;

    // Reserve space for efficiency
    buf.reserve(buf.size() + triangle_count * 3 * 7);

    for (size_t t = 0; t < triangle_count; ++t) {
        for (size_t v = 0; v < 3; ++v) {
            uint32_t idx = indices[t * 3 + v];
            // Position (3 floats)
            buf.push_back(vertices[idx * 3 + 0]);
            buf.push_back(vertices[idx * 3 + 1]);
            buf.push_back(vertices[idx * 3 + 2]);
            // Color (4 floats) - same for all vertices
            buf.push_back(color.r);
            buf.push_back(color.g);
            buf.push_back(color.b);
            buf.push_back(color.a);
        }
    }
}

// ============================================================
// Wireframe primitives
// ============================================================

void ImmediateRenderer::polyline(const std::vector<Vec3>& points, const Color4& color, bool closed, bool depth_test) {
    if (points.size() < 2) return;
    for (size_t i = 0; i < points.size() - 1; ++i) {
        line(points[i], points[i + 1], color, depth_test);
    }
    if (closed && points.size() > 2) {
        line(points.back(), points.front(), color, depth_test);
    }
}

void ImmediateRenderer::circle(
    const Vec3& center,
    const Vec3& normal,
    double radius,
    const Color4& color,
    int segments,
    bool depth_test
) {
    Vec3 norm = normal.normalized();
    auto [tangent, bitangent] = _build_basis(norm);

    std::vector<Vec3> points;
    points.reserve(segments);

    for (int i = 0; i < segments; ++i) {
        double angle = 2.0 * M_PI * i / segments;
        Vec3 point = center + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        points.push_back(point);
    }

    polyline(points, color, true, depth_test);
}

void ImmediateRenderer::arrow(
    const Vec3& origin,
    const Vec3& direction,
    double length,
    const Color4& color,
    double head_length,
    double head_width,
    bool depth_test
) {
    Vec3 dir = direction.normalized();
    Vec3 tip = origin + dir * length;
    Vec3 head_base = tip - dir * (length * head_length);

    // Shaft
    line(origin, head_base, color, depth_test);

    // Head (4 lines)
    auto [right, up] = _build_basis(dir);

    double hw = length * head_width;
    Vec3 p1 = head_base + right * hw;
    Vec3 p2 = head_base - right * hw;
    Vec3 p3 = head_base + up * hw;
    Vec3 p4 = head_base - up * hw;

    line(tip, p1, color, depth_test);
    line(tip, p2, color, depth_test);
    line(tip, p3, color, depth_test);
    line(tip, p4, color, depth_test);
}

void ImmediateRenderer::box(const Vec3& min_pt, const Vec3& max_pt, const Color4& color, bool depth_test) {
    // 8 corners
    Vec3 corners[8] = {
        {min_pt.x, min_pt.y, min_pt.z},
        {max_pt.x, min_pt.y, min_pt.z},
        {max_pt.x, max_pt.y, min_pt.z},
        {min_pt.x, max_pt.y, min_pt.z},
        {min_pt.x, min_pt.y, max_pt.z},
        {max_pt.x, min_pt.y, max_pt.z},
        {max_pt.x, max_pt.y, max_pt.z},
        {min_pt.x, max_pt.y, max_pt.z},
    };

    // 12 edges
    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},  // bottom
        {4, 5}, {5, 6}, {6, 7}, {7, 4},  // top
        {0, 4}, {1, 5}, {2, 6}, {3, 7},  // vertical
    };

    for (auto& e : edges) {
        line(corners[e[0]], corners[e[1]], color, depth_test);
    }
}

void ImmediateRenderer::cylinder_wireframe(
    const Vec3& start,
    const Vec3& end,
    double radius,
    const Color4& color,
    int segments,
    bool depth_test
) {
    Vec3 axis = end - start;
    double length = axis.norm();
    if (length < 1e-6) return;
    axis = axis / length;

    // Circles at ends
    circle(start, axis, radius, color, segments, depth_test);
    circle(end, axis, radius, color, segments, depth_test);

    // Connecting lines
    auto [tangent, bitangent] = _build_basis(axis);

    for (int i = 0; i < 4; ++i) {
        double angle = 2.0 * M_PI * i / 4;
        Vec3 offset = (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        line(start + offset, end + offset, color, depth_test);
    }
}

void ImmediateRenderer::sphere_wireframe(
    const Vec3& center,
    double radius,
    const Color4& color,
    int segments,
    bool depth_test
) {
    // 3 orthogonal circles
    circle(center, Vec3{0, 0, 1}, radius, color, segments, depth_test);
    circle(center, Vec3{0, 1, 0}, radius, color, segments, depth_test);
    circle(center, Vec3{1, 0, 0}, radius, color, segments, depth_test);
}

void ImmediateRenderer::capsule_wireframe(
    const Vec3& start,
    const Vec3& end,
    double radius,
    const Color4& color,
    int segments,
    bool depth_test
) {
    Vec3 axis = end - start;
    double length = axis.norm();
    if (length < 1e-6) {
        sphere_wireframe(start, radius, color, segments, depth_test);
        return;
    }
    axis = axis / length;

    auto [tangent, bitangent] = _build_basis(axis);

    // Circles at ends
    circle(start, axis, radius, color, segments, depth_test);
    circle(end, axis, radius, color, segments, depth_test);

    // Connecting lines
    for (int i = 0; i < 4; ++i) {
        double angle = 2.0 * M_PI * i / 4;
        Vec3 offset = (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        line(start + offset, end + offset, color, depth_test);
    }

    // Hemisphere arcs
    int half_segments = segments / 2;

    for (const Vec3* basis_vec : {&tangent, &bitangent}) {
        // Arc at start
        std::vector<Vec3> points_start;
        points_start.reserve(half_segments + 1);
        for (int i = 0; i <= half_segments; ++i) {
            double angle = M_PI * i / half_segments;
            Vec3 pt = start + (*basis_vec * std::cos(angle) - axis * std::sin(angle)) * radius;
            points_start.push_back(pt);
        }
        polyline(points_start, color, false, depth_test);

        // Arc at end
        std::vector<Vec3> points_end;
        points_end.reserve(half_segments + 1);
        for (int i = 0; i <= half_segments; ++i) {
            double angle = M_PI * i / half_segments;
            Vec3 pt = end + (*basis_vec * std::cos(angle) + axis * std::sin(angle)) * radius;
            points_end.push_back(pt);
        }
        polyline(points_end, color, false, depth_test);
    }
}

// ============================================================
// Solid primitives
// ============================================================

void ImmediateRenderer::cylinder_solid(
    const Vec3& start,
    const Vec3& end,
    double radius,
    const Color4& color,
    int segments,
    bool caps,
    bool depth_test
) {
    Vec3 axis = end - start;
    double length = axis.norm();
    if (length < 1e-6) return;
    axis = axis / length;

    auto [tangent, bitangent] = _build_basis(axis);

    // Generate ring points
    std::vector<Vec3> ring_start, ring_end;
    ring_start.reserve(segments);
    ring_end.reserve(segments);

    for (int i = 0; i < segments; ++i) {
        double angle = 2.0 * M_PI * i / segments;
        Vec3 offset = (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        ring_start.push_back(start + offset);
        ring_end.push_back(end + offset);
    }

    // Side triangles
    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        triangle(ring_start[i], ring_end[i], ring_end[j], color, depth_test);
        triangle(ring_start[i], ring_end[j], ring_start[j], color, depth_test);
    }

    // Caps
    if (caps) {
        for (int i = 0; i < segments; ++i) {
            int j = (i + 1) % segments;
            triangle(start, ring_start[j], ring_start[i], color, depth_test);
            triangle(end, ring_end[i], ring_end[j], color, depth_test);
        }
    }
}

void ImmediateRenderer::cone_solid(
    const Vec3& base,
    const Vec3& tip,
    double radius,
    const Color4& color,
    int segments,
    bool cap,
    bool depth_test
) {
    Vec3 axis = tip - base;
    double length = axis.norm();
    if (length < 1e-6) return;
    axis = axis / length;

    auto [tangent, bitangent] = _build_basis(axis);

    // Generate base ring points
    std::vector<Vec3> ring;
    ring.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        double angle = 2.0 * M_PI * i / segments;
        Vec3 offset = (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        ring.push_back(base + offset);
    }

    // Side triangles
    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        triangle(ring[i], tip, ring[j], color, depth_test);
    }

    // Base cap
    if (cap) {
        for (int i = 0; i < segments; ++i) {
            int j = (i + 1) % segments;
            triangle(base, ring[j], ring[i], color, depth_test);
        }
    }
}

void ImmediateRenderer::torus_solid(
    const TorusSolidSpec& spec,
    const Color4& color,
    bool depth_test
) {
    Vec3 ax = spec.axis.normalized();
    auto [tangent, bitangent] = _build_basis(ax);

    // Generate torus vertices
    std::vector<std::vector<Vec3>> vertices(spec.major_segments);

    for (int i = 0; i < spec.major_segments; ++i) {
        double theta = 2.0 * M_PI * i / spec.major_segments;
        Vec3 ring_center = spec.center +
            (tangent * std::cos(theta) + bitangent * std::sin(theta)) * spec.major_radius;
        Vec3 radial = tangent * std::cos(theta) + bitangent * std::sin(theta);

        vertices[i].reserve(spec.minor_segments);
        for (int j = 0; j < spec.minor_segments; ++j) {
            double phi = 2.0 * M_PI * j / spec.minor_segments;
            Vec3 point = ring_center + (radial * std::cos(phi) + ax * std::sin(phi)) * spec.minor_radius;
            vertices[i].push_back(point);
        }
    }

    // Generate triangles
    for (int i = 0; i < spec.major_segments; ++i) {
        int i_next = (i + 1) % spec.major_segments;
        for (int j = 0; j < spec.minor_segments; ++j) {
            int j_next = (j + 1) % spec.minor_segments;
            const Vec3& p00 = vertices[i][j];
            const Vec3& p10 = vertices[i_next][j];
            const Vec3& p01 = vertices[i][j_next];
            const Vec3& p11 = vertices[i_next][j_next];
            triangle(p00, p10, p11, color, depth_test);
            triangle(p00, p11, p01, color, depth_test);
        }
    }
}

void ImmediateRenderer::arrow_solid(
    const ArrowSolidSpec& spec,
    const Color4& color,
    bool depth_test
) {
    double dir_len = spec.direction.norm();
    if (dir_len < 1e-6) return;
    Vec3 dir = spec.direction / dir_len;

    double head_length = spec.length * spec.head_length_ratio;
    double shaft_length = spec.length - head_length;

    Vec3 shaft_end = spec.origin + dir * shaft_length;
    Vec3 tip = spec.origin + dir * spec.length;

    // Shaft cylinder
    cylinder_solid(spec.origin, shaft_end, spec.shaft_radius, color, spec.segments, true, depth_test);
    // Head cone
    cone_solid(shaft_end, tip, spec.head_radius, color, spec.segments, true, depth_test);
}

// ============================================================
// Rendering
// ============================================================

void ImmediateRenderer::_ensure_shader(tgfx::IRenderDevice* device) {
    _device = device;
    // Engine VS+FS via the tc_shader registry — hash-based dedup keeps
    // the compiled modules alive across RenderContext2 re-creations
    // (happens on Play/Stop when a new game viewport spins up its own
    // ctx2 and constructs a fresh ImmediateRenderer).
    if (tc_shader_handle_is_invalid(_shader_handle)) {
        _shader_handle = tgfx::register_builtin_shader_from_catalog(IMMEDIATE_ENGINE_SHADER_UUID);
    }
}

void ImmediateRenderer::_flush_buffers(
    tgfx::RenderContext2* ctx2,
    std::vector<float>& lines,
    std::vector<float>& tris,
    const Mat44& view_matrix,
    const Mat44& proj_matrix,
    bool depth_test,
    bool blend
) {
    if (!ctx2) return;
    if (lines.empty() && tris.empty()) return;

    _ensure_shader(&ctx2->device());
    tgfx::ShaderHandle imm_vs, imm_fs;
    tc_shader* imm_raw = nullptr;
    {
        imm_raw = tc_shader_get(_shader_handle);
        if (!imm_raw || !tc_shader_ensure_tgfx2(imm_raw, _device, &imm_vs, &imm_fs)) {
            return;
        }
    }

    ctx2->set_depth_test(depth_test);
    ctx2->set_depth_write(depth_test);
    ctx2->set_blend(blend);
    if (blend) {
        ctx2->set_blend_func(tgfx::BlendFactor::SrcAlpha,
                             tgfx::BlendFactor::OneMinusSrcAlpha);
    }
    ctx2->set_cull(tgfx::CullMode::None);
    ctx2->bind_shader(imm_vs, imm_fs);
    ctx2->use_shader_resource_layout(imm_raw);

    // View-projection combined on CPU: shader only needs one matrix,
    // fits comfortably in 128-byte push constants. Double→float narrow
    // happens here because Mat44 internal storage is double.
    Mat44 vp = proj_matrix * view_matrix;
    ImmediatePushData push{};
    for (int i = 0; i < 16; ++i) {
        push.u_vp[i] = static_cast<float>(vp.data[i]);
    }
    ctx2->bind_uniform_data("u_push", &push, static_cast<uint32_t>(sizeof(push)));
    if (!lines.empty()) {
        uint32_t vertex_count = static_cast<uint32_t>(lines.size() / 7);
        ctx2->draw_immediate_lines(lines.data(), vertex_count);
        lines.clear();
    }

    if (!tris.empty()) {
        uint32_t vertex_count = static_cast<uint32_t>(tris.size() / 7);
        ctx2->draw_immediate_triangles(tris.data(), vertex_count);
        tris.clear();
    }
}

void ImmediateRenderer::flush(
    tgfx::RenderContext2* ctx2,
    const Mat44& view_matrix,
    const Mat44& proj_matrix,
    bool depth_test,
    bool blend
) {
    if (line_vertices.empty() && tri_vertices.empty()) return;

    bool profile = tc_profiler_enabled();
    if (profile) tc_profiler_begin_section("Immediate:Flush");
    _flush_buffers(ctx2, line_vertices, tri_vertices, view_matrix, proj_matrix, depth_test, blend);
    if (profile) tc_profiler_end_section();
}

void ImmediateRenderer::flush_depth(
    tgfx::RenderContext2* ctx2,
    const Mat44& view_matrix,
    const Mat44& proj_matrix,
    bool blend
) {
    if (line_vertices_depth.empty() && tri_vertices_depth.empty()) return;

    bool profile = tc_profiler_enabled();
    if (profile) tc_profiler_begin_section("Immediate:FlushDepth");
    _flush_buffers(ctx2, line_vertices_depth, tri_vertices_depth, view_matrix, proj_matrix, true, blend);
    if (profile) tc_profiler_end_section();
}

} // namespace termin
