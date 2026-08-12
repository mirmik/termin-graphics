#include "termin_visual_scene/items/point_cloud_item3d.hpp"

#include "item_geometry3d_internal.hpp"

#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        bool valid_cloud(const PointCloudData3D& cloud) {
            return !cloud.points.empty() &&
                   std::all_of(cloud.points.begin(), cloud.points.end(), [](const auto& point) {
                       return detail::finite(point.position) && detail::finite(point.color) &&
                              std::isfinite(point.size_scale) && point.size_scale > 0.0f;
                   });
        }

        void require_cloud(const std::shared_ptr<const PointCloudData3D>& cloud) {
            if (!cloud || !valid_cloud(*cloud)) {
                tc::Log::error("PointCloudItem3D rejected invalid point data replacement");
                throw std::invalid_argument("PointCloudItem3D requires finite non-empty point data");
            }
        }

        void require_style(const tgfx::PointCloudStyle& style) {
            if (!std::isfinite(style.size_px) || style.size_px <= 0.0f || !detail::finite(style.tint) ||
                (style.shape != tgfx::PointCloudShape::Square && style.shape != tgfx::PointCloudShape::Circle)) {
                tc::Log::error("PointCloudItem3D rejected an invalid style");
                throw std::invalid_argument("PointCloudItem3D style is invalid");
            }
        }

        void require_radius(double radius) {
            if (!std::isfinite(radius) || radius <= 0.0) {
                tc::Log::error("PointCloudItem3D rejected an invalid pick radius");
                throw std::invalid_argument("PointCloudItem3D pick radius must be positive and finite");
            }
        }

    } // namespace

    PointCloudItem3D::PointCloudItem3D(std::shared_ptr<const PointCloudData3D> cloud,
                                       tgfx::PointCloudStyle style,
                                       double pick_radius)
        : NativeVisualItem3D("termin.visual.PointCloud3D") {
        set_cloud(std::move(cloud));
        set_style(style);
        set_pick_radius(pick_radius);
    }

    void PointCloudItem3D::set_cloud(std::shared_ptr<const PointCloudData3D> cloud) {
        require_cloud(cloud);
        cloud_ = std::move(cloud);
    }

    void PointCloudItem3D::set_style(tgfx::PointCloudStyle style) {
        require_style(style);
        style_ = style;
    }

    void PointCloudItem3D::set_pick_radius(double radius) {
        require_radius(radius);
        pick_radius_ = radius;
    }

    std::optional<VisualBounds3D> PointCloudItem3D::local_bounds() const {
        auto bounds =
            detail::bounds_of(cloud_->points.size(), [&](std::size_t index) { return cloud_->points[index].position; });
        if (!bounds)
            return std::nullopt;
        const auto largest =
            std::max_element(cloud_->points.begin(), cloud_->points.end(), [](const auto& left, const auto& right) {
                return left.size_scale < right.size_scale;
            });
        const double radius = pick_radius_ * largest->size_scale;
        bounds->min -= termin::Vec3{radius, radius, radius};
        bounds->max += termin::Vec3{radius, radius, radius};
        return bounds;
    }

    std::optional<HitCandidate3D> PointCloudItem3D::hit_test(const HitTestContext3D& context) const {
        std::optional<HitCandidate3D> nearest;
        for (std::size_t index = 0; index < cloud_->points.size(); ++index) {
            const auto distance = detail::ray_sphere(
                context.local_ray, cloud_->points[index].position, pick_radius_ * cloud_->points[index].size_scale);
            if (!distance || (nearest && *distance >= nearest->distance))
                continue;
            nearest = HitCandidate3D{*distance, index + 1};
        }
        return nearest;
    }

    bool PointCloudItem3D::paint(GraphicItemPaintContext3D& context) const {
        const PointCloudDrawPacket3D packet{cloud_, style_};
        return context.submit(PointCloudDrawProtocol3D, &packet, sizeof(packet));
    }

} // namespace termin::visual
