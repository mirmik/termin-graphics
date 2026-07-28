#pragma once

#include <optional>

#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

class TERMIN_VISUAL_SCENE_API PathItem2D final
    : public NativeGraphicItem2D {
public:
    PathItem2D();
    PathItem2D(
        tgfx::Path2f path,
        std::optional<tgfx::FillPaint> fill,
        std::optional<tgfx::StrokePaint> stroke = std::nullopt);

    void set_path(tgfx::Path2f path);
    void set_fill(std::optional<tgfx::FillPaint> fill);
    void set_stroke(std::optional<tgfx::StrokePaint> stroke);

    std::optional<termin::Bounds2f> local_bounds() const;
    bool hit_test(termin::Vec2f point, float tolerance) const;
    bool paint(GraphicItemPaintContext2D& context) const override;

private:
    tgfx::Path2f path_;
    std::optional<tgfx::FillPaint> fill_;
    std::optional<tgfx::StrokePaint> stroke_;
};

}  // namespace termin::visual
