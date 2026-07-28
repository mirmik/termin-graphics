#include "termin_visual_scene/native_graphic_item2d.hpp"

#include "graphic_item_draw_sink_internal.hpp"

namespace termin::visual {

const tc_graphic_item_vtable NativeGraphicItem2D::VTABLE{
    .type_name = "termin.visual.NativeGraphicItem2D",
    .local_bounds = dispatch_local_bounds,
    .hit_test = dispatch_hit_test,
    .paint = dispatch_paint,
    .push_clip = dispatch_push_clip,
    .clip_contains = dispatch_clip_contains,
    .on_destroy = dispatch_on_destroy,
};

NativeGraphicItem2D::NativeGraphicItem2D(const char* type_name)
    : GraphicItem2D(&VTABLE, type_name) {}

NativeGraphicItem2D* NativeGraphicItem2D::from_c(
    tc_graphic_item* item)
{
    return item != nullptr
        ? static_cast<NativeGraphicItem2D*>(item->body)
        : nullptr;
}

const NativeGraphicItem2D* NativeGraphicItem2D::from_c(
    const tc_graphic_item* item)
{
    return item != nullptr
        ? static_cast<const NativeGraphicItem2D*>(item->body)
        : nullptr;
}

bool NativeGraphicItem2D::dispatch_local_bounds(
    const tc_graphic_item* item,
    tc_bounds2f* out_bounds)
{
    const auto* self = from_c(item);
    if (self == nullptr || out_bounds == nullptr) return false;
    const auto bounds = self->local_bounds();
    if (!bounds) return false;
    *out_bounds = *bounds;
    return true;
}

bool NativeGraphicItem2D::dispatch_hit_test(
    const tc_graphic_item* item,
    tc_vec2f point,
    float tolerance)
{
    const auto* self = from_c(item);
    return self != nullptr &&
        self->hit_test(point, tolerance);
}

bool NativeGraphicItem2D::dispatch_paint(
    const tc_graphic_item* item,
    tc_graphic_item_draw_sink* sink)
{
    const auto* self = from_c(item);
    if (self == nullptr || sink == nullptr) return false;
    GraphicItemPaintContext2D context(sink);
    return self->paint(context);
}

bool NativeGraphicItem2D::dispatch_push_clip(
    const tc_graphic_item* item,
    tc_graphic_item_draw_sink* sink,
    bool* out_pushed)
{
    const auto* self = from_c(item);
    if (self == nullptr || sink == nullptr ||
        sink->builder == nullptr || out_pushed == nullptr) {
        return false;
    }
    *out_pushed = false;
    const auto& clip = self->clip();
    if (!clip) return true;
    if (!sink->builder->push_clip(
            clip->path, clip->rule)) {
        return false;
    }
    *out_pushed = true;
    return true;
}

bool NativeGraphicItem2D::dispatch_clip_contains(
    const tc_graphic_item* item,
    tc_vec2f local_point)
{
    const auto* self = from_c(item);
    if (self == nullptr) return false;
    const auto& clip = self->clip();
    return !clip ||
        clip->path.flatten().contains(
            local_point, clip->rule);
}

void NativeGraphicItem2D::dispatch_on_destroy(
    tc_graphic_item* item,
    tc_visual_scene* scene)
{
    auto* self = from_c(item);
    if (self != nullptr) self->on_destroy(scene);
}

}  // namespace termin::visual
