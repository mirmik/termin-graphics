#include "termin_visual_scene/native_visual_item3d.hpp"

#include <exception>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        template <typename Callback>
        bool guard_item_callback(const tc_visual_item3d* item, const char* operation, Callback&& callback) {
            try {
                return callback();
            } catch (const std::exception& error) {
                tc::Log::error(
                    "visual item3d '%s' %s failed: %s", tc_visual_item3d_type_name(item), operation, error.what());
            } catch (...) {
                tc::Log::error("visual item3d '%s' %s failed with an unknown exception",
                               tc_visual_item3d_type_name(item),
                               operation);
            }
            return false;
        }

    } // namespace

    const tc_visual_item3d_vtable NativeVisualItem3D::VTABLE{
        .type_name = "termin.visual.NativeVisualItem3D",
        .on_destroy = dispatch_on_destroy,
        .hit_test = dispatch_hit_test,
        .local_bounds = dispatch_local_bounds,
        .paint = dispatch_paint,
    };

    NativeVisualItem3D::NativeVisualItem3D(const char* type_name)
        : VisualItem3D(&VTABLE, type_name) {}

    NativeVisualItem3D* NativeVisualItem3D::from_c(tc_visual_item3d* item) {
        return item != nullptr ? static_cast<NativeVisualItem3D*>(item->body) : nullptr;
    }

    const NativeVisualItem3D* NativeVisualItem3D::from_c(const tc_visual_item3d* item) {
        return item != nullptr ? static_cast<const NativeVisualItem3D*>(item->body) : nullptr;
    }

    void NativeVisualItem3D::dispatch_on_destroy(tc_visual_item3d* item, tc_visual_scene3d* scene) {
        auto* self = from_c(item);
        if (self == nullptr)
            return;
        guard_item_callback(item, "on_destroy", [&] {
            self->on_destroy(scene);
            return true;
        });
    }

    bool NativeVisualItem3D::dispatch_hit_test(const tc_visual_item3d* item,
                                               const tc_visual_hit_test_context3d* context,
                                               tc_visual_hit_candidate3d* out_candidate) {
        const auto* self = from_c(item);
        if (self == nullptr || context == nullptr || out_candidate == nullptr)
            return false;
        return guard_item_callback(item, "hit_test", [&] {
            const auto hit = self->hit_test(*context);
            if (!hit)
                return false;
            *out_candidate = *hit;
            return true;
        });
    }

    bool NativeVisualItem3D::dispatch_local_bounds(const tc_visual_item3d* item, tc_visual_bounds3d* out_bounds) {
        const auto* self = from_c(item);
        if (self == nullptr || out_bounds == nullptr)
            return false;
        return guard_item_callback(item, "local_bounds", [&] {
            const auto bounds = self->local_bounds();
            if (!bounds)
                return false;
            *out_bounds = *bounds;
            return true;
        });
    }

    bool NativeVisualItem3D::dispatch_paint(const tc_visual_item3d* item, tc_visual_item_paint_context3d* raw_context) {
        const auto* self = from_c(item);
        if (self == nullptr || raw_context == nullptr)
            return false;
        return guard_item_callback(item, "paint", [&] {
            GraphicItemPaintContext3D context(raw_context);
            return self->paint(context);
        });
    }

} // namespace termin::visual
