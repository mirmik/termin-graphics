#pragma once

#include <optional>

#include <termin/geom/rect2.hpp>
#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

class TERMIN_VISUAL_SCENE_API RoundedRectItem2D final
    : public NativeGraphicItem2D {
public:
    RoundedRectItem2D();
    RoundedRectItem2D(
        termin::Rect2f rect,
        float radius,
        tgfx::FillPaint fill,
        std::optional<tgfx::StrokePaint> stroke = std::nullopt);

    void set_rect(termin::Rect2f rect);
    void set_radius(float radius);
    void set_fill(tgfx::FillPaint fill);
    void set_stroke(std::optional<tgfx::StrokePaint> stroke);

    std::optional<termin::Bounds2f> local_bounds() const;
    bool hit_test(termin::Vec2f point, float tolerance) const;
    bool paint(GraphicItemPaintContext2D& context) const override;

private:
    termin::Rect2f rect_{};
    float radius_ = 0.0f;
    tgfx::FillPaint fill_{};
    std::optional<tgfx::StrokePaint> stroke_;
};

}  // namespace termin::visual
