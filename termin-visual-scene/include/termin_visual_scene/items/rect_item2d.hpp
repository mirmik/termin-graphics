#pragma once

#include <optional>

#include <termin/geom/rect2.hpp>
#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API RectItem2D final : public NativeGraphicItem2D {
    public:
        RectItem2D();
        RectItem2D(termin::Rect2f rect, tgfx::FillPaint fill, std::optional<tgfx::StrokePaint> stroke = std::nullopt);

        termin::Rect2f rect() const noexcept {
            return rect_;
        }
        void set_rect(termin::Rect2f rect);
        const tgfx::FillPaint& fill() const noexcept {
            return fill_;
        }
        void set_fill(tgfx::FillPaint fill);
        const std::optional<tgfx::StrokePaint>& stroke() const noexcept {
            return stroke_;
        }
        void set_stroke(std::optional<tgfx::StrokePaint> stroke);

        std::optional<termin::Bounds2f> local_bounds() const;
        bool hit_test(termin::Vec2f point, float tolerance) const;
        bool paint(GraphicItemPaintContext2D& context) const override;

    private:
        termin::Rect2f rect_{};
        tgfx::FillPaint fill_{};
        std::optional<tgfx::StrokePaint> stroke_;
    };

} // namespace termin::visual
