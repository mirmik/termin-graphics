#pragma once

#include <string>

#include <termin/geom/rect2.hpp>
#include <tgfx2/draw_list2d.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API ImageItem2D final : public NativeGraphicItem2D {
    public:
        ImageItem2D();
        ImageItem2D(std::string image_uri,
                    termin::Rect2f rect,
                    termin::Rect2f uv,
                    tgfx::Color4f tint,
                    tgfx::DrawTextureSampling2D sampling);

        void set_image_uri(std::string uri);
        void set_rect(termin::Rect2f rect);
        void set_uv(termin::Rect2f uv);
        void set_tint(tgfx::Color4f tint);
        void set_sampling(tgfx::DrawTextureSampling2D sampling);

        std::optional<termin::Bounds2f> local_bounds() const;
        bool hit_test(termin::Vec2f point, float tolerance) const;
        bool paint(GraphicItemPaintContext2D& context) const override;

    private:
        std::string image_uri_;
        termin::Rect2f rect_{};
        termin::Rect2f uv_{0.0f, 0.0f, 1.0f, 1.0f};
        tgfx::Color4f tint_{};
        tgfx::DrawTextureSampling2D sampling_ = tgfx::DrawTextureSampling2D::Linear;
    };

} // namespace termin::visual
