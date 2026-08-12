#pragma once

#include "termin_visual_scene/items/item3d_packets.hpp"
#include "termin_visual_scene/native_visual_item3d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API PointCloudItem3D final : public NativeVisualItem3D {
    public:
        explicit PointCloudItem3D(std::shared_ptr<const PointCloudData3D> cloud,
                                  tgfx::PointCloudStyle style = {},
                                  double pick_radius = 0.05);

        const std::shared_ptr<const PointCloudData3D>& cloud() const noexcept {
            return cloud_;
        }
        void set_cloud(std::shared_ptr<const PointCloudData3D> cloud);
        const tgfx::PointCloudStyle& style() const noexcept {
            return style_;
        }
        void set_style(tgfx::PointCloudStyle style);
        double pick_radius() const noexcept {
            return pick_radius_;
        }
        void set_pick_radius(double radius);

        std::optional<VisualBounds3D> local_bounds() const override;
        std::optional<HitCandidate3D> hit_test(const HitTestContext3D& context) const override;
        bool paint(GraphicItemPaintContext3D& context) const override;

    private:
        std::shared_ptr<const PointCloudData3D> cloud_;
        tgfx::PointCloudStyle style_{};
        double pick_radius_ = 0.05;
    };

} // namespace termin::visual
