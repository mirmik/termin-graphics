#include "termin_visual_scene/items/ellipse_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

void validate(
    termin::Rect2f bounds,
    const tgfx::FillPaint& fill,
    const std::optional<tgfx::StrokePaint>& stroke)
{
    if (!detail::valid_rect(bounds) ||
        !fill.validate() ||
        (stroke && !stroke->validate())) {
        throw std::invalid_argument(
            "invalid EllipseItem2D state");
    }
}

}  // namespace

EllipseItem2D::EllipseItem2D()
    : NativeGraphicItem2D(
          "termin.visual.Ellipse2D") {
}

EllipseItem2D::EllipseItem2D(
    termin::Rect2f bounds,
    tgfx::FillPaint fill,
    std::optional<tgfx::StrokePaint> stroke)
    : EllipseItem2D() {
    validate(bounds, fill, stroke);
    bounds_ = bounds;
    fill_ = std::move(fill);
    stroke_ = std::move(stroke);
}

void EllipseItem2D::set_bounds(
    termin::Rect2f bounds)
{
    validate(bounds, fill_, stroke_);
    bounds_ = bounds;
}

void EllipseItem2D::set_fill(
    tgfx::FillPaint fill)
{
    validate(bounds_, fill, stroke_);
    fill_ = std::move(fill);
}

void EllipseItem2D::set_stroke(
    std::optional<tgfx::StrokePaint> stroke)
{
    validate(bounds_, fill_, stroke);
    stroke_ = std::move(stroke);
}

std::optional<termin::Bounds2f>
EllipseItem2D::local_bounds() const {
    auto result = detail::rect_bounds(bounds_);
    return stroke_
        ? detail::expanded(
              result, stroke_->width * 0.5f)
        : result;
}

bool EllipseItem2D::hit_test(
    termin::Vec2f point,
    float) const
{
    return detail::ellipse_contains(bounds_, point);
}

bool EllipseItem2D::paint(
    GraphicItemPaintContext2D& context) const
{
    return context.ellipse(
        bounds_, fill_, stroke_);
}

}  // namespace termin::visual
