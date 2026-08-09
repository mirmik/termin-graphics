#pragma once

#include <string>

#include <tgfx2/draw_list2d.hpp>
#include <termin/geom/color.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API TextItem2D final : public NativeGraphicItem2D {
    public:
        TextItem2D();
        TextItem2D(std::string text,
                   std::string font_uri,
                   termin::Vec2f origin,
                   float size_px,
                   termin::SrgbColor color,
                   tgfx::TextAnchor2D anchor,
                   termin::Bounds2f layout_bounds);

        void set_text(std::string text);
        void set_font_uri(std::string font_uri);
        void set_origin(termin::Vec2f origin);
        void set_size_px(float size_px);
        void set_color(termin::SrgbColor color);
        void set_anchor(tgfx::TextAnchor2D anchor);
        void set_layout_bounds(termin::Bounds2f bounds);

        std::optional<termin::Bounds2f> local_bounds() const;
        bool hit_test(termin::Vec2f point, float tolerance) const;
        bool paint(GraphicItemPaintContext2D& context) const override;

    private:
        std::string text_;
        std::string font_uri_;
        termin::Vec2f origin_{};
        float size_px_ = 14.0f;
        termin::SrgbColor color_{};
        tgfx::TextAnchor2D anchor_ = tgfx::TextAnchor2D::Left;
        termin::Bounds2f layout_bounds_{};
    };

} // namespace termin::visual
