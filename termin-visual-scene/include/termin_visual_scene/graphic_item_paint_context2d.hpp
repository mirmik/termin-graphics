#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <termin/geom/color.hpp>
#include <termin/geom/rect2.hpp>
#include <tgfx2/draw_list2d.hpp>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/tc_graphic_item.h"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API GraphicItemPaintContext2D {
    public:
        explicit GraphicItemPaintContext2D(tc_graphic_item_draw_sink* sink) noexcept
            : sink_(sink) {}

        bool rect(termin::Rect2f rect, tgfx::FillPaint fill);
        bool rounded_rect(termin::Rect2f rect,
                          float radius,
                          tgfx::FillPaint fill,
                          std::optional<tgfx::StrokePaint> stroke = std::nullopt);
        bool
        ellipse(termin::Rect2f bounds, tgfx::FillPaint fill, std::optional<tgfx::StrokePaint> stroke = std::nullopt);
        bool path(tgfx::Path2f path,
                  std::optional<tgfx::FillPaint> fill,
                  std::optional<tgfx::StrokePaint> stroke = std::nullopt);
        bool polyline(std::span<const termin::Vec2f> points, tgfx::StrokePaint stroke, bool closed = false);
        bool text(std::string text,
                  std::string font_uri,
                  termin::Vec2f origin,
                  float size_px,
                  termin::SrgbColor color,
                  tgfx::TextAnchor2D anchor = tgfx::TextAnchor2D::Left,
                  std::optional<float> coverage_gamma = std::nullopt);
        bool image(std::string image_uri,
                   termin::Rect2f rect,
                   termin::Rect2f uv = {0.0f, 0.0f, 1.0f, 1.0f},
                   termin::SrgbColor tint = termin::SrgbColor::white(),
                   tgfx::DrawTextureSampling2D sampling = tgfx::DrawTextureSampling2D::Linear);
        bool custom_batch(std::string key, termin::Bounds2f local_bounds);
        bool retained_batch(std::shared_ptr<tgfx::RetainedDrawBatch2D> batch);
        bool push_clip_rect(termin::Rect2f rect);
        bool pop_clip();

    private:
        tc_graphic_item_draw_sink* sink_ = nullptr;
    };

} // namespace termin::visual
