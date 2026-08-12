#include "termin_visual_scene/items/group_item3d.hpp"

namespace termin::visual {

    GroupItem3D::GroupItem3D()
        : NativeVisualItem3D("termin.visual.Group3D") {}

    std::optional<VisualBounds3D> GroupItem3D::local_bounds() const {
        return std::nullopt;
    }

    std::optional<HitCandidate3D> GroupItem3D::hit_test(const HitTestContext3D&) const {
        return std::nullopt;
    }

    bool GroupItem3D::paint(GraphicItemPaintContext3D&) const {
        return true;
    }

} // namespace termin::visual
