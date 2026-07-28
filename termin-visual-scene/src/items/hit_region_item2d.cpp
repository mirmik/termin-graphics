#include "termin_visual_scene/items/hit_region_item2d.hpp"


#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

void validate(
    const tgfx::Path2f& path,
    tgfx::FillRule rule)
{
    if (path.empty() ||
        (rule != tgfx::FillRule::NonZero &&
         rule != tgfx::FillRule::EvenOdd)) {
        throw std::invalid_argument(
            "invalid HitRegionItem2D state");
    }
}

}  // namespace

HitRegionItem2D::HitRegionItem2D()
    : NativeGraphicItem2D(
          "termin.visual.HitRegion2D") {
}

HitRegionItem2D::HitRegionItem2D(
    tgfx::Path2f path,
    tgfx::FillRule rule)
    : HitRegionItem2D() {
    validate(path, rule);
    path_ = std::move(path);
    rule_ = rule;
}

void HitRegionItem2D::set_path(tgfx::Path2f path) {
    validate(path, rule_);
    path_ = std::move(path);
}

void HitRegionItem2D::set_rule(tgfx::FillRule rule) {
    validate(path_, rule);
    rule_ = rule;
}

std::optional<termin::Bounds2f>
HitRegionItem2D::local_bounds() const {
    return path_.empty()
        ? std::nullopt
        : std::optional<termin::Bounds2f>(
              path_.bounds());
}

bool HitRegionItem2D::hit_test(
    termin::Vec2f point,
    float) const
{
    return !path_.empty() &&
        path_.flatten().contains(point, rule_);
}

bool HitRegionItem2D::paint(
    GraphicItemPaintContext2D&) const
{
    return true;
}

}  // namespace termin::visual
