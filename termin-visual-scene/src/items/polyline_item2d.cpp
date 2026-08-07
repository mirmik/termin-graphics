#include "termin_visual_scene/items/polyline_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <algorithm>
#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        void validate(const std::vector<termin::Vec2f>& points, const tgfx::StrokePaint& stroke) {
            if (points.size() < 2 || !stroke.validate() ||
                !std::all_of(points.begin(), points.end(), detail::valid_point)) {
                throw std::invalid_argument("invalid PolylineItem2D state");
            }
        }

        tgfx::Path2f hit_path(const std::vector<termin::Vec2f>& points, bool closed) {
            tgfx::Path2f path;
            path.move_to(points.front());
            for (std::size_t i = 1; i < points.size(); ++i) {
                path.line_to(points[i]);
            }
            if (closed)
                path.close();
            return path;
        }

    } // namespace

    PolylineItem2D::PolylineItem2D()
        : NativeGraphicItem2D("termin.visual.Polyline2D") {}

    PolylineItem2D::PolylineItem2D(std::vector<termin::Vec2f> points, tgfx::StrokePaint stroke, bool closed)
        : PolylineItem2D() {
        set(std::move(points), std::move(stroke), closed);
    }

    void PolylineItem2D::set(std::vector<termin::Vec2f> points, tgfx::StrokePaint stroke, bool closed) {
        validate(points, stroke);
        points_ = std::move(points);
        stroke_ = std::move(stroke);
        closed_ = closed;
    }

    void PolylineItem2D::set_points(std::vector<termin::Vec2f> points) {
        validate(points, stroke_);
        points_ = std::move(points);
    }

    void PolylineItem2D::set_stroke(tgfx::StrokePaint stroke) {
        validate(points_, stroke);
        stroke_ = std::move(stroke);
    }

    void PolylineItem2D::set_closed(bool closed) {
        closed_ = closed;
    }

    std::optional<termin::Bounds2f> PolylineItem2D::local_bounds() const {
        if (points_.empty())
            return std::nullopt;
        termin::Bounds2f result{
            points_.front().x,
            points_.front().y,
            points_.front().x,
            points_.front().y,
        };
        for (const auto point : points_) {
            result.x0 = std::min(result.x0, point.x);
            result.y0 = std::min(result.y0, point.y);
            result.x1 = std::max(result.x1, point.x);
            result.y1 = std::max(result.y1, point.y);
        }
        return detail::expanded(result, stroke_.width * 0.5f);
    }

    bool PolylineItem2D::hit_test(termin::Vec2f point, float) const {
        return !points_.empty() && hit_path(points_, closed_).flatten().stroke_contains(point, stroke_);
    }

    bool PolylineItem2D::paint(GraphicItemPaintContext2D& context) const {
        return context.polyline(points_, stroke_, closed_);
    }

} // namespace termin::visual
