#include "tgfx2/line_mesh_builder.hpp"

#include <algorithm>
#include <cmath>

namespace tgfx {
namespace {

constexpr float kEpsilon = 1.0e-6f;
constexpr float kPi = 3.14159265358979323846f;

LinePoint3 normalize_or_zero(LinePoint3 v) {
    const float len = v.norm();
    if (len <= kEpsilon) {
        return {};
    }
    return v / len;
}

bool same_point(LinePoint3 a, LinePoint3 b) {
    return (a - b).norm() <= kEpsilon;
}

LinePoint3 safe_side(LinePoint3 dir, LinePoint3 up_hint) {
    LinePoint3 up = normalize_or_zero(up_hint);
    if (up.norm() <= kEpsilon) {
        up = LinePoint3::unit_y();
    }

    LinePoint3 side = normalize_or_zero(up.cross(dir));
    if (side.norm() > kEpsilon) {
        return side;
    }

    side = normalize_or_zero(LinePoint3::unit_x().cross(dir));
    if (side.norm() > kEpsilon) {
        return side;
    }

    return normalize_or_zero(LinePoint3::unit_z().cross(dir));
}

uint32_t append_vertex(LineMesh& mesh, LinePoint3 p) {
    const uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(LineVertex{p});
    return index;
}

void append_triangle(LineMesh& mesh, LinePoint3 a, LinePoint3 b, LinePoint3 c) {
    const uint32_t ia = append_vertex(mesh, a);
    const uint32_t ib = append_vertex(mesh, b);
    const uint32_t ic = append_vertex(mesh, c);
    mesh.indices.push_back(ia);
    mesh.indices.push_back(ib);
    mesh.indices.push_back(ic);
}

void append_quad(LineMesh& mesh, LinePoint3 a, LinePoint3 b, LinePoint3 c, LinePoint3 d) {
    append_triangle(mesh, a, b, c);
    append_triangle(mesh, a, c, d);
}

void append_round_disk(LineMesh& mesh, LinePoint3 center, LinePoint3 axis_a, LinePoint3 axis_b,
                       float radius, int segments) {
    segments = std::clamp(segments, 6, 64);
    LinePoint3 prev = center + axis_a * radius;
    for (int i = 1; i <= segments; ++i) {
        const float t = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
        const float c = std::cos(t);
        const float s = std::sin(t);
        LinePoint3 next = center + (axis_a * c + axis_b * s) * radius;
        append_triangle(mesh, center, prev, next);
        prev = next;
    }
}

void append_round_cap(LineMesh& mesh, LinePoint3 center, LinePoint3 side, LinePoint3 outward,
                      float radius, int segments) {
    segments = std::clamp(segments, 4, 64);
    LinePoint3 prev = center + side * radius;
    for (int i = 1; i <= segments; ++i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(segments);
        const float c = std::cos(t);
        const float s = std::sin(t);
        LinePoint3 rim = side * c + outward * s;
        LinePoint3 next = center + rim * radius;
        append_triangle(mesh, center, prev, next);
        prev = next;
    }
}

struct Segment {
    LinePoint3 p0;
    LinePoint3 p1;
    LinePoint3 dir;
    LinePoint3 side;
};

} // namespace

LineMesh build_line_mesh(std::span<const LinePoint3> points,
                         const LineStyle& style) {
    LineMesh mesh;
    if (points.size() < 2 || style.width <= 0.0f) {
        return mesh;
    }

    std::vector<LinePoint3> clean_points;
    clean_points.reserve(points.size() + (style.closed ? 1 : 0));
    for (LinePoint3 p : points) {
        if (clean_points.empty() || !same_point(clean_points.back(), p)) {
            clean_points.push_back(p);
        }
    }

    if (style.closed && clean_points.size() > 2 && !same_point(clean_points.front(), clean_points.back())) {
        clean_points.push_back(clean_points.front());
    }

    if (clean_points.size() < 2) {
        return mesh;
    }

    const float half_width = style.width * 0.5f;
    std::vector<Segment> segments;
    segments.reserve(clean_points.size() - 1);
    for (size_t i = 1; i < clean_points.size(); ++i) {
        LinePoint3 p0 = clean_points[i - 1];
        LinePoint3 p1 = clean_points[i];
        LinePoint3 delta = p1 - p0;
        const float len = delta.norm();
        if (len <= kEpsilon) {
            continue;
        }
        LinePoint3 dir = delta / len;
        LinePoint3 side = safe_side(dir, style.up_hint);
        if (side.norm() <= kEpsilon) {
            continue;
        }
        segments.push_back({p0, p1, dir, side});
    }

    if (segments.empty()) {
        return mesh;
    }

    for (size_t i = 0; i < segments.size(); ++i) {
        Segment seg = segments[i];
        if (style.cap == LineCapStyle::Square && i == 0 && !style.closed) {
            seg.p0 -= seg.dir * half_width;
        }
        if (style.cap == LineCapStyle::Square && i + 1 == segments.size() && !style.closed) {
            seg.p1 += seg.dir * half_width;
        }

        const LinePoint3 offset = seg.side * half_width;
        append_quad(mesh,
                    seg.p0 + offset,
                    seg.p0 - offset,
                    seg.p1 - offset,
                    seg.p1 + offset);
    }

    if (!style.closed && style.cap == LineCapStyle::Round) {
        const Segment& first = segments.front();
        append_round_cap(mesh, first.p0, first.side, -first.dir,
                         half_width, style.round_segments);

        const Segment& last = segments.back();
        append_round_cap(mesh, last.p1, -last.side, last.dir,
                         half_width, style.round_segments);
    }

    if (style.join == LineJoinStyle::Round) {
        for (size_t i = 1; i < clean_points.size() - 1; ++i) {
            const size_t seg_index = std::min(i, segments.size() - 1);
            const Segment& seg = segments[seg_index];
            append_round_disk(mesh, clean_points[i], seg.side, seg.dir,
                              half_width, style.round_segments);
        }
    }

    return mesh;
}

} // namespace tgfx
