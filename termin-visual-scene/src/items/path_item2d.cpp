#include "termin_visual_scene/items/path_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        void validate(const tgfx::Path2f& path,
                      const std::optional<tgfx::FillPaint>& fill,
                      const std::optional<tgfx::StrokePaint>& stroke) {
            if (path.empty() || (!fill && !stroke) || (fill && !fill->validate()) || (stroke && !stroke->validate())) {
                throw std::invalid_argument("invalid PathItem2D state");
            }
        }

    } // namespace

    PathItem2D::PathItem2D()
        : NativeGraphicItem2D("termin.visual.Path2D") {}

    PathItem2D::PathItem2D(tgfx::Path2f path,
                           std::optional<tgfx::FillPaint> fill,
                           std::optional<tgfx::StrokePaint> stroke)
        : PathItem2D() {
        validate(path, fill, stroke);
        path_ = std::move(path);
        fill_ = std::move(fill);
        stroke_ = std::move(stroke);
    }

    void PathItem2D::set_path(tgfx::Path2f path) {
        validate(path, fill_, stroke_);
        path_ = std::move(path);
    }

    void PathItem2D::set_fill(std::optional<tgfx::FillPaint> fill) {
        validate(path_, fill, stroke_);
        fill_ = std::move(fill);
    }

    void PathItem2D::set_stroke(std::optional<tgfx::StrokePaint> stroke) {
        validate(path_, fill_, stroke);
        stroke_ = std::move(stroke);
    }

    std::optional<termin::Bounds2f> PathItem2D::local_bounds() const {
        auto result = path_.bounds();
        return stroke_ ? detail::merged(result, path_.stroke_bounds(*stroke_)) : result;
    }

    bool PathItem2D::hit_test(termin::Vec2f point, float) const {
        const auto flat = path_.flatten();
        return (fill_ && flat.contains(point, fill_->rule)) || (stroke_ && flat.stroke_contains(point, *stroke_));
    }

    bool PathItem2D::paint(GraphicItemPaintContext2D& context) const {
        return context.path(path_, fill_, stroke_);
    }

} // namespace termin::visual
