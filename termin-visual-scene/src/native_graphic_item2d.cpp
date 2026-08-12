#include "termin_visual_scene/native_graphic_item2d.hpp"

#include "graphic_item_draw_sink_internal.hpp"

namespace termin::visual {

    const tc_graphic_item_vtable NativeGraphicItem2D::VTABLE{
        .type_name = "termin.visual.NativeGraphicItem2D",
        .local_bounds = dispatch_local_bounds,
        .hit_test = dispatch_hit_test,
        .paint = dispatch_paint,
        .composition_clip = dispatch_composition_clip,
        .on_destroy = dispatch_on_destroy,
    };

    NativeGraphicItem2D::NativeGraphicItem2D(const char* type_name)
        : GraphicItem2D(&VTABLE, type_name) {}

    NativeGraphicItem2D* NativeGraphicItem2D::from_c(tc_graphic_item* item) {
        return item != nullptr ? static_cast<NativeGraphicItem2D*>(item->body) : nullptr;
    }

    const NativeGraphicItem2D* NativeGraphicItem2D::from_c(const tc_graphic_item* item) {
        return item != nullptr ? static_cast<const NativeGraphicItem2D*>(item->body) : nullptr;
    }

    bool NativeGraphicItem2D::dispatch_local_bounds(const tc_graphic_item* item, tc_bounds2f* out_bounds) {
        const auto* self = from_c(item);
        if (self == nullptr || out_bounds == nullptr)
            return false;
        const auto bounds = self->local_bounds();
        if (!bounds)
            return false;
        *out_bounds = *bounds;
        return true;
    }

    bool NativeGraphicItem2D::dispatch_hit_test(const tc_graphic_item* item, tc_vec2f point, float tolerance) {
        const auto* self = from_c(item);
        return self != nullptr && self->hit_test(point, tolerance);
    }

    bool NativeGraphicItem2D::dispatch_paint(const tc_graphic_item* item, tc_graphic_item_draw_sink* sink) {
        const auto* self = from_c(item);
        if (self == nullptr || sink == nullptr)
            return false;
        GraphicItemPaintContext2D context(sink);
        return self->paint(context);
    }

    bool NativeGraphicItem2D::dispatch_composition_clip(const tc_graphic_item* item,
                                                        tc_graphic_item_clip2d_view* out_clip) {
        const auto* self = from_c(item);
        if (self == nullptr || out_clip == nullptr) {
            return false;
        }
        const auto& clip = self->clip();
        if (!clip)
            return false;
        static_assert(sizeof(tgfx::Path2Verb) == sizeof(std::uint8_t));
        *out_clip = {
            reinterpret_cast<const std::uint8_t*>(clip->path.verbs().data()),
            clip->path.verbs().size(),
            clip->path.points().data(),
            clip->path.points().size(),
            static_cast<std::uint8_t>(clip->rule),
        };
        return true;
    }

    void NativeGraphicItem2D::dispatch_on_destroy(tc_graphic_item* item, tc_visual_scene* scene) {
        auto* self = from_c(item);
        if (self != nullptr)
            self->on_destroy(scene);
    }

} // namespace termin::visual
