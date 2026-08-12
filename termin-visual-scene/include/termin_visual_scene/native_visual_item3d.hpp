#pragma once

#include <optional>

#include "termin_visual_scene/paint3d.hpp"
#include "termin_visual_scene/visual_item3d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API NativeVisualItem3D : public VisualItem3D {
    public:
        explicit NativeVisualItem3D(const char* type_name);
        ~NativeVisualItem3D() override = default;

        virtual std::optional<VisualBounds3D> local_bounds() const = 0;
        virtual std::optional<HitCandidate3D> hit_test(const HitTestContext3D& context) const = 0;
        virtual bool paint(GraphicItemPaintContext3D& context) const = 0;
        virtual void on_destroy(tc_visual_scene3d*) {}

    private:
        static const tc_visual_item3d_vtable VTABLE;

        static NativeVisualItem3D* from_c(tc_visual_item3d* item);
        static const NativeVisualItem3D* from_c(const tc_visual_item3d* item);
        static void dispatch_on_destroy(tc_visual_item3d* item, tc_visual_scene3d* scene);
        static bool dispatch_hit_test(const tc_visual_item3d* item,
                                      const tc_visual_hit_test_context3d* context,
                                      tc_visual_hit_candidate3d* out_candidate);
        static bool dispatch_local_bounds(const tc_visual_item3d* item, tc_visual_bounds3d* out_bounds);
        static bool dispatch_paint(const tc_visual_item3d* item, tc_visual_item_paint_context3d* context);
    };

} // namespace termin::visual
