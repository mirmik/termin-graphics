#pragma once

#include <optional>

#include <termin/geom/rect2.hpp>
#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API EllipseItem2D final : public NativeGraphicItem2D {
    public:
        EllipseItem2D();
        EllipseItem2D(termin::Rect2f bounds,
                      tgfx::FillPaint fill,
                      std::optional<tgfx::StrokePaint> stroke = std::nullopt);

        void set_bounds(termin::Rect2f bounds);
        void set_fill(tgfx::FillPaint fill);
        void set_stroke(std::optional<tgfx::StrokePaint> stroke);

        std::optional<termin::Bounds2f> local_bounds() const;
        bool hit_test(termin::Vec2f point, float tolerance) const;
        bool paint(GraphicItemPaintContext2D& context) const override;

    private:
        termin::Rect2f bounds_{};
        tgfx::FillPaint fill_{};
        std::optional<tgfx::StrokePaint> stroke_;
    };

} // namespace termin::visual
