#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include <termin/geom/color.hpp>
#include <termin/geom/vec3.hpp>

#include "termin_visual_scene/scene3d.hpp"

namespace termin::visual::detail {

    inline bool finite(termin::Vec3f value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    inline bool finite(termin::LinearColor value) {
        return std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b) && std::isfinite(value.a);
    }

    inline termin::Vec3 to_double(termin::Vec3f value) {
        return {value.x, value.y, value.z};
    }

    template <typename Position> std::optional<VisualBounds3D> bounds_of(std::size_t count, Position&& position) {
        if (count == 0)
            return std::nullopt;
        const termin::Vec3 first = to_double(position(0));
        VisualBounds3D result{first, first};
        for (std::size_t index = 1; index < count; ++index) {
            const termin::Vec3 point = to_double(position(index));
            result.min.x = std::min(result.min.x, point.x);
            result.min.y = std::min(result.min.y, point.y);
            result.min.z = std::min(result.min.z, point.z);
            result.max.x = std::max(result.max.x, point.x);
            result.max.y = std::max(result.max.y, point.y);
            result.max.z = std::max(result.max.z, point.z);
        }
        return result;
    }

    inline std::optional<double> ray_triangle(tc_ray3 ray, termin::Vec3f af, termin::Vec3f bf, termin::Vec3f cf) {
        const termin::Vec3 a = to_double(af);
        const termin::Vec3 b = to_double(bf);
        const termin::Vec3 c = to_double(cf);
        const termin::Vec3 edge1 = b - a;
        const termin::Vec3 edge2 = c - a;
        const termin::Vec3 p = ray.direction.cross(edge2);
        const double determinant = edge1.dot(p);
        if (std::abs(determinant) <= 1.0e-12)
            return std::nullopt;
        const double inverse = 1.0 / determinant;
        const termin::Vec3 offset = ray.origin - a;
        const double u = offset.dot(p) * inverse;
        if (u < 0.0 || u > 1.0)
            return std::nullopt;
        const termin::Vec3 q = offset.cross(edge1);
        const double v = ray.direction.dot(q) * inverse;
        if (v < 0.0 || u + v > 1.0)
            return std::nullopt;
        const double distance = edge2.dot(q) * inverse;
        return distance > 0.0 && std::isfinite(distance) ? std::optional<double>(distance) : std::nullopt;
    }

    template <typename Position>
    std::optional<HitCandidate3D> ray_triangles(tc_ray3 ray,
                                                std::size_t vertex_count,
                                                std::span<const std::uint32_t> triangles,
                                                std::span<const std::uint64_t> parts,
                                                Position&& position) {
        std::optional<HitCandidate3D> nearest;
        for (std::size_t triangle = 0; triangle < triangles.size() / 3; ++triangle) {
            const std::uint32_t i0 = triangles[triangle * 3];
            const std::uint32_t i1 = triangles[triangle * 3 + 1];
            const std::uint32_t i2 = triangles[triangle * 3 + 2];
            if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
                continue;
            const auto distance = ray_triangle(ray, position(i0), position(i1), position(i2));
            if (!distance || (nearest && *distance >= nearest->distance))
                continue;
            nearest = HitCandidate3D{*distance, parts.empty() ? triangle + 1 : parts[triangle]};
        }
        return nearest;
    }

    inline std::optional<double> ray_sphere(tc_ray3 ray, termin::Vec3f centerf, double radius) {
        const termin::Vec3 offset = ray.origin - to_double(centerf);
        const double a = ray.direction.dot(ray.direction);
        const double half_b = offset.dot(ray.direction);
        const double c = offset.dot(offset) - radius * radius;
        const double discriminant = half_b * half_b - a * c;
        if (!std::isfinite(discriminant) || discriminant < 0.0 || a <= 1.0e-24)
            return std::nullopt;
        const double root = std::sqrt(discriminant);
        const double near_distance = (-half_b - root) / a;
        if (near_distance > 0.0)
            return near_distance;
        const double far_distance = (-half_b + root) / a;
        return far_distance > 0.0 ? std::optional<double>(far_distance) : std::nullopt;
    }

} // namespace termin::visual::detail
