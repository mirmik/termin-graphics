#include "termin_visual_scene/items/custom_batch_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        void validate(const std::string& key, termin::Bounds2f bounds) {
            if (key.empty() || !detail::valid_bounds(bounds)) {
                throw std::invalid_argument("invalid CustomBatchItem2D state");
            }
        }

    } // namespace

    CustomBatchItem2D::CustomBatchItem2D()
        : NativeGraphicItem2D("termin.visual.CustomBatch2D") {}

    CustomBatchItem2D::CustomBatchItem2D(std::string key, termin::Bounds2f local_bounds)
        : CustomBatchItem2D() {
        validate(key, local_bounds);
        key_ = std::move(key);
        local_bounds_ = local_bounds;
    }

    void CustomBatchItem2D::set_key(std::string key) {
        validate(key, local_bounds_);
        key_ = std::move(key);
    }

    void CustomBatchItem2D::set_local_bounds(termin::Bounds2f bounds) {
        validate(key_, bounds);
        local_bounds_ = bounds;
    }

    std::optional<termin::Bounds2f> CustomBatchItem2D::local_bounds() const {
        return detail::valid_bounds(local_bounds_) ? std::optional<termin::Bounds2f>(local_bounds_) : std::nullopt;
    }

    bool CustomBatchItem2D::hit_test(termin::Vec2f point, float) const {
        return detail::bounds_contains(local_bounds_, point);
    }

    bool CustomBatchItem2D::paint(GraphicItemPaintContext2D& context) const {
        return context.custom_batch(key_, local_bounds_);
    }

} // namespace termin::visual
