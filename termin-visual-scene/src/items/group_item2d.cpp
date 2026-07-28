#include "termin_visual_scene/items/group_item2d.hpp"


namespace termin::visual {

GroupItem2D::GroupItem2D()
    : NativeGraphicItem2D("termin.visual.Group2D") {
}

std::optional<termin::Bounds2f>
GroupItem2D::local_bounds() const {
    return std::nullopt;
}

bool GroupItem2D::hit_test(
    termin::Vec2f,
    float) const
{
    return false;
}

bool GroupItem2D::paint(
    GraphicItemPaintContext2D&) const
{
    return true;
}

}  // namespace termin::visual
