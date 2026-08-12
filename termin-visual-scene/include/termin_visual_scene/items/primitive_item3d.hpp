#pragma once

#include "termin_visual_scene/items/item3d_packets.hpp"
#include "termin_visual_scene/native_visual_item3d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API PrimitiveItem3D final : public NativeVisualItem3D {
    public:
        explicit PrimitiveItem3D(std::shared_ptr<const PrimitiveGeometry3D> geometry, bool depth_test = true);

        const std::shared_ptr<const PrimitiveGeometry3D>& geometry() const noexcept {
            return geometry_;
        }
        void set_geometry(std::shared_ptr<const PrimitiveGeometry3D> geometry);
        bool depth_test() const noexcept {
            return depth_test_;
        }
        void set_depth_test(bool value) noexcept {
            depth_test_ = value;
        }

        std::optional<VisualBounds3D> local_bounds() const override;
        std::optional<HitCandidate3D> hit_test(const HitTestContext3D& context) const override;
        bool paint(GraphicItemPaintContext3D& context) const override;

    private:
        std::shared_ptr<const PrimitiveGeometry3D> geometry_;
        bool depth_test_ = true;
    };

} // namespace termin::visual
