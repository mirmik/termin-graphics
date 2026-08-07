#pragma once

#include <string>

#include "termin_visual_scene/native_graphic_item2d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API CustomBatchItem2D final : public NativeGraphicItem2D {
    public:
        CustomBatchItem2D();
        CustomBatchItem2D(std::string key, termin::Bounds2f local_bounds);

        void set_key(std::string key);
        void set_local_bounds(termin::Bounds2f bounds);

        std::optional<termin::Bounds2f> local_bounds() const;
        bool hit_test(termin::Vec2f point, float tolerance) const;
        bool paint(GraphicItemPaintContext2D& context) const override;

    private:
        std::string key_;
        termin::Bounds2f local_bounds_{};
    };

} // namespace termin::visual
