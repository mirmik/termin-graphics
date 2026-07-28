#pragma once

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

class TERMIN_VISUAL_SCENE_API GroupItem2D final
    : public NativeGraphicItem2D {
public:
    GroupItem2D();

    std::optional<termin::Bounds2f> local_bounds() const;
    bool hit_test(termin::Vec2f point, float tolerance) const;
    bool paint(GraphicItemPaintContext2D& context) const override;
};

}  // namespace termin::visual
