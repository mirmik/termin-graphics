#pragma once

#include "termin_visual_scene/native_visual_item3d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API GroupItem3D final : public NativeVisualItem3D {
    public:
        GroupItem3D();

        std::optional<VisualBounds3D> local_bounds() const override;
        std::optional<HitCandidate3D> hit_test(const HitTestContext3D& context) const override;
        bool paint(GraphicItemPaintContext3D& context) const override;
    };

} // namespace termin::visual
