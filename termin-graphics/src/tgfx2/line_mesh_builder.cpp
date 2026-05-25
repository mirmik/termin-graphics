#include "tgfx2/line_mesh_builder.hpp"

#include <algorithm>
#include <cmath>

namespace tgfx {
namespace {

constexpr float kEpsilon = 1.0e-6f;
constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 to_vec(LinePoint3 p) {
    return {p.x, p.y, p.z};
}

LinePoint3 to_point(Vec3 v) {
    return {v.x, v.y, v.z};
}

Vec3 add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 mul(Vec3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float length(Vec3 v) {
    return std::sqrt(dot(v, v));
}

Vec3 normalize(Vec3 v) {
    const float len = length(v);
    if (len <= kEpsilon) {
        return {};
    }
    return mul(v, 1.0f / len);
}

bool same_point(Vec3 a, Vec3 b) {
    return length(sub(a, b)) <= kEpsilon;
}

Vec3 safe_side(Vec3 dir, Vec3 up_hint) {
    Vec3 up = normalize(up_hint);
    if (length(up) <= kEpsilon) {
        up = {0.0f, 1.0f, 0.0f};
    }

    Vec3 side = normalize(cross(up, dir));
    if (length(side) > kEpsilon) {
        return side;
    }

    side = normalize(cross(Vec3{1.0f, 0.0f, 0.0f}, dir));
    if (length(side) > kEpsilon) {
        return side;
    }

    return normalize(cross(Vec3{0.0f, 0.0f, 1.0f}, dir));
}

uint32_t append_vertex(LineMesh& mesh, Vec3 p) {
    const uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(LineVertex{to_point(p)});
    return index;
}

void append_triangle(LineMesh& mesh, Vec3 a, Vec3 b, Vec3 c) {
    const uint32_t ia = append_vertex(mesh, a);
    const uint32_t ib = append_vertex(mesh, b);
    const uint32_t ic = append_vertex(mesh, c);
    mesh.indices.push_back(ia);
    mesh.indices.push_back(ib);
    mesh.indices.push_back(ic);
}

void append_quad(LineMesh& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    append_triangle(mesh, a, b, c);
    append_triangle(mesh, a, c, d);
}

void append_round_disk(LineMesh& mesh, Vec3 center, Vec3 axis_a, Vec3 axis_b,
                       float radius, int segments) {
    segments = std::clamp(segments, 6, 64);
    Vec3 prev = add(center, mul(axis_a, radius));
    for (int i = 1; i <= segments; ++i) {
        const float t = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
        Vec3 next = add(center, mul(add(mul(axis_a, std::cos(t)),
                                     mul(axis_b, std::sin(t))), radius));
        append_triangle(mesh, center, prev, next);
        prev = next;
    }
}

void append_round_cap(LineMesh& mesh, Vec3 center, Vec3 side, Vec3 outward,
                      float radius, int segments) {
    segments = std::clamp(segments, 4, 64);
    Vec3 prev = add(center, mul(side, radius));
    for (int i = 1; i <= segments; ++i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(segments);
        Vec3 rim = add(mul(side, std::cos(t)), mul(outward, std::sin(t)));
        Vec3 next = add(center, mul(rim, radius));
        append_triangle(mesh, center, prev, next);
        prev = next;
    }
}

struct Segment {
    Vec3 p0;
    Vec3 p1;
    Vec3 dir;
    Vec3 side;
};

} // namespace

LineMesh build_line_mesh(std::span<const LinePoint3> points,
                         const LineStyle& style) {
    LineMesh mesh;
    if (points.size() < 2 || style.width <= 0.0f) {
        return mesh;
    }

    std::vector<Vec3> clean_points;
    clean_points.reserve(points.size() + (style.closed ? 1 : 0));
    for (LinePoint3 p : points) {
        Vec3 v = to_vec(p);
        if (clean_points.empty() || !same_point(clean_points.back(), v)) {
            clean_points.push_back(v);
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
        Vec3 p0 = clean_points[i - 1];
        Vec3 p1 = clean_points[i];
        Vec3 delta = sub(p1, p0);
        const float len = length(delta);
        if (len <= kEpsilon) {
            continue;
        }
        Vec3 dir = mul(delta, 1.0f / len);
        Vec3 side = safe_side(dir, to_vec(style.up_hint));
        if (length(side) <= kEpsilon) {
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
            seg.p0 = sub(seg.p0, mul(seg.dir, half_width));
        }
        if (style.cap == LineCapStyle::Square && i + 1 == segments.size() && !style.closed) {
            seg.p1 = add(seg.p1, mul(seg.dir, half_width));
        }

        const Vec3 offset = mul(seg.side, half_width);
        append_quad(mesh,
                    add(seg.p0, offset),
                    sub(seg.p0, offset),
                    sub(seg.p1, offset),
                    add(seg.p1, offset));
    }

    if (!style.closed && style.cap == LineCapStyle::Round) {
        const Segment& first = segments.front();
        append_round_cap(mesh, first.p0, first.side, mul(first.dir, -1.0f),
                         half_width, style.round_segments);

        const Segment& last = segments.back();
        append_round_cap(mesh, last.p1, mul(last.side, -1.0f), last.dir,
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
