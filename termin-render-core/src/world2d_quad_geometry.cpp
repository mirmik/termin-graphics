#include <termin/render/world2d_quad_geometry.hpp>

#include <cmath>
#include <limits>

namespace termin {
    namespace {

        bool ray_intersects_triangle(
            const Vec3& origin, const Vec3& direction, const Vec3& a, const Vec3& b, const Vec3& c, double& distance) {
            constexpr double epsilon = 1.0e-10;
            const Vec3 edge1 = b - a;
            const Vec3 edge2 = c - a;
            const Vec3 p = direction.cross(edge2);
            const double determinant = edge1.dot(p);
            if (std::abs(determinant) <= epsilon) {
                return false;
            }
            const double inverse_determinant = 1.0 / determinant;
            const Vec3 t = origin - a;
            const double u = t.dot(p) * inverse_determinant;
            if (u < 0.0 || u > 1.0) {
                return false;
            }
            const Vec3 q = t.cross(edge1);
            const double v = direction.dot(q) * inverse_determinant;
            if (v < 0.0 || u + v > 1.0) {
                return false;
            }
            const double candidate = edge2.dot(q) * inverse_determinant;
            if (candidate < 0.0) {
                return false;
            }
            distance = candidate;
            return true;
        }

    } // namespace

    std::array<Vec3, 4> world2d_quad_corners(const World2DQuadRect& local_rect, const Mat44& model) {
        // On the XZ plane, this winding has normal -Y.
        return {
            model.transform_point({local_rect.min_x, 0.0, local_rect.min_z}),
            model.transform_point({local_rect.min_x, 0.0, local_rect.max_z}),
            model.transform_point({local_rect.max_x, 0.0, local_rect.max_z}),
            model.transform_point({local_rect.max_x, 0.0, local_rect.min_z}),
        };
    }

    AABB world2d_quad_bounds(const World2DQuadRect& local_rect, const Mat44& model) {
        const auto corners = world2d_quad_corners(local_rect, model);
        return AABB::from_points(corners.data(), corners.size());
    }

    bool ray_intersects_world2d_quad(const Vec3& ray_origin,
                                     const Vec3& ray_direction,
                                     const World2DQuadRect& local_rect,
                                     const Mat44& model,
                                     double* out_distance) {
        const double direction_length = ray_direction.norm();
        if (!std::isfinite(direction_length) || direction_length <= 1.0e-12) {
            return false;
        }

        const auto corners = world2d_quad_corners(local_rect, model);
        double first = std::numeric_limits<double>::infinity();
        double second = std::numeric_limits<double>::infinity();
        const bool hit_first =
            ray_intersects_triangle(ray_origin, ray_direction, corners[0], corners[1], corners[2], first);
        const bool hit_second =
            ray_intersects_triangle(ray_origin, ray_direction, corners[0], corners[2], corners[3], second);
        if (!hit_first && !hit_second) {
            return false;
        }
        if (out_distance) {
            *out_distance = std::min(first, second);
        }
        return true;
    }

} // namespace termin
