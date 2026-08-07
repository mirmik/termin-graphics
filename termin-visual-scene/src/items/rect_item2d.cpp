#include "termin_visual_scene/items/rect_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        void
        validate(termin::Rect2f rect, const tgfx::FillPaint& fill, const std::optional<tgfx::StrokePaint>& stroke) {
            if (!detail::valid_rect(rect) || !fill.validate() || (stroke && !stroke->validate())) {
                throw std::invalid_argument("invalid RectItem2D state");
            }
        }

    } // namespace

    RectItem2D::RectItem2D()
        : NativeGraphicItem2D("termin.visual.Rect2D") {}

    RectItem2D::RectItem2D(termin::Rect2f rect, tgfx::FillPaint fill, std::optional<tgfx::StrokePaint> stroke)
        : RectItem2D() {
        validate(rect, fill, stroke);
        rect_ = rect;
        fill_ = std::move(fill);
        stroke_ = std::move(stroke);
    }

    void RectItem2D::set_rect(termin::Rect2f rect) {
        validate(rect, fill_, stroke_);
        rect_ = rect;
    }

    void RectItem2D::set_fill(tgfx::FillPaint fill) {
        validate(rect_, fill, stroke_);
        fill_ = std::move(fill);
    }

    void RectItem2D::set_stroke(std::optional<tgfx::StrokePaint> stroke) {
        validate(rect_, fill_, stroke);
        stroke_ = std::move(stroke);
    }

    std::optional<termin::Bounds2f> RectItem2D::local_bounds() const {
        auto result = detail::rect_bounds(rect_);
        return stroke_ ? detail::expanded(result, stroke_->width * 0.5f) : result;
    }

    bool RectItem2D::hit_test(termin::Vec2f point, float) const {
        return detail::rect_contains(rect_, point);
    }

    bool RectItem2D::paint(GraphicItemPaintContext2D& context) const {
        return stroke_ ? context.rounded_rect(rect_, 0.0f, fill_, stroke_) : context.rect(rect_, fill_);
    }

} // namespace termin::visual
