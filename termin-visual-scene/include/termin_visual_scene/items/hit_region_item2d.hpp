#pragma once

#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API HitRegionItem2D final : public NativeGraphicItem2D {
    public:
        HitRegionItem2D();
        HitRegionItem2D(tgfx::Path2f path, tgfx::FillRule rule = tgfx::FillRule::NonZero);

        void set_path(tgfx::Path2f path);
        void set_rule(tgfx::FillRule rule);

        std::optional<termin::Bounds2f> local_bounds() const;
        bool hit_test(termin::Vec2f point, float tolerance) const;
        bool paint(GraphicItemPaintContext2D& context) const override;

    private:
        tgfx::Path2f path_;
        tgfx::FillRule rule_ = tgfx::FillRule::NonZero;
    };

} // namespace termin::visual
