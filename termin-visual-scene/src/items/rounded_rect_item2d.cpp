#include "termin_visual_scene/items/rounded_rect_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <cmath>
#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

void validate(
    termin::Rect2f rect,
    float radius,
    const tgfx::FillPaint& fill,
    const std::optional<tgfx::StrokePaint>& stroke)
{
    if (!detail::valid_rect(rect) ||
        !std::isfinite(radius) || radius < 0.0f ||
        !fill.validate() ||
        (stroke && !stroke->validate())) {
        throw std::invalid_argument(
            "invalid RoundedRectItem2D state");
    }
}

}  // namespace

RoundedRectItem2D::RoundedRectItem2D()
    : NativeGraphicItem2D(
          "termin.visual.RoundedRect2D") {
}

RoundedRectItem2D::RoundedRectItem2D(
    termin::Rect2f rect,
    float radius,
    tgfx::FillPaint fill,
    std::optional<tgfx::StrokePaint> stroke)
    : RoundedRectItem2D() {
    validate(rect, radius, fill, stroke);
    rect_ = rect;
    radius_ = radius;
    fill_ = std::move(fill);
    stroke_ = std::move(stroke);
}

void RoundedRectItem2D::set_rect(
    termin::Rect2f rect)
{
    validate(rect, radius_, fill_, stroke_);
    rect_ = rect;
}

void RoundedRectItem2D::set_radius(float radius) {
    validate(rect_, radius, fill_, stroke_);
    radius_ = radius;
}

void RoundedRectItem2D::set_fill(
    tgfx::FillPaint fill)
{
    validate(rect_, radius_, fill, stroke_);
    fill_ = std::move(fill);
}

void RoundedRectItem2D::set_stroke(
    std::optional<tgfx::StrokePaint> stroke)
{
    validate(rect_, radius_, fill_, stroke);
    stroke_ = std::move(stroke);
}

std::optional<termin::Bounds2f>
RoundedRectItem2D::local_bounds() const {
    auto result = detail::rect_bounds(rect_);
    return stroke_
        ? detail::expanded(
              result, stroke_->width * 0.5f)
        : result;
}

bool RoundedRectItem2D::hit_test(
    termin::Vec2f point,
    float) const
{
    return detail::rounded_rect_contains(
        rect_, radius_, point);
}

bool RoundedRectItem2D::paint(
    GraphicItemPaintContext2D& context) const
{
    return context.rounded_rect(
        rect_, radius_, fill_, stroke_);
}

}  // namespace termin::visual
