#pragma once

#include "termin_visual_scene/graphic_item2d.hpp"
#include "termin_visual_scene/graphic_item_paint_context2d.hpp"

namespace termin::visual {

class TERMIN_VISUAL_SCENE_API NativeGraphicItem2D
    : public GraphicItem2D {
public:
    explicit NativeGraphicItem2D(const char* type_name);
    ~NativeGraphicItem2D() override = default;

    virtual std::optional<termin::Bounds2f> local_bounds() const = 0;
    virtual bool paint(GraphicItemPaintContext2D& context) const = 0;
    virtual bool hit_test(
        termin::Vec2f point,
        float tolerance) const = 0;
    virtual void on_destroy(tc_visual_scene*) {}

private:
    static const tc_graphic_item_vtable VTABLE;

    static NativeGraphicItem2D* from_c(tc_graphic_item* item);
    static const NativeGraphicItem2D* from_c(
        const tc_graphic_item* item);
    static bool dispatch_local_bounds(
        const tc_graphic_item* item,
        tc_bounds2f* out_bounds);
    static bool dispatch_hit_test(
        const tc_graphic_item* item,
        tc_vec2f point,
        float tolerance);
    static bool dispatch_paint(
        const tc_graphic_item* item,
        tc_graphic_item_draw_sink* sink);
    static bool dispatch_push_clip(
        const tc_graphic_item* item,
        tc_graphic_item_draw_sink* sink,
        bool* out_pushed);
    static bool dispatch_clip_contains(
        const tc_graphic_item* item,
        tc_vec2f local_point);
    static void dispatch_on_destroy(
        tc_graphic_item* item,
        tc_visual_scene* scene);
};

}  // namespace termin::visual
