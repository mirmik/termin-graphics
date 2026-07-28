#pragma once

#include <vector>

#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

class TERMIN_VISUAL_SCENE_API PolylineItem2D final
    : public NativeGraphicItem2D {
public:
    PolylineItem2D();
    PolylineItem2D(
        std::vector<termin::Vec2f> points,
        tgfx::StrokePaint stroke,
        bool closed = false);

    void set(
        std::vector<termin::Vec2f> points,
        tgfx::StrokePaint stroke,
        bool closed);
    void set_points(std::vector<termin::Vec2f> points);
    void set_stroke(tgfx::StrokePaint stroke);
    void set_closed(bool closed);

    std::optional<termin::Bounds2f> local_bounds() const;
    bool hit_test(termin::Vec2f point, float tolerance) const;
    bool paint(GraphicItemPaintContext2D& context) const override;

private:
    std::vector<termin::Vec2f> points_;
    tgfx::StrokePaint stroke_{};
    bool closed_ = false;
};

}  // namespace termin::visual
