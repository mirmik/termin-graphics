#include "termin_visual_scene/items/image_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <stdexcept>
#include <cmath>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {
        bool finite_color(termin::SrgbColor color) {
            return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) && std::isfinite(color.a);
        }

        void validate(const std::string& uri,
                      termin::Rect2f rect,
                      termin::Rect2f uv,
                      termin::SrgbColor tint,
                      tgfx::DrawTextureSampling2D sampling) {
            if (uri.empty() || !detail::valid_rect(rect) || !detail::valid_rect(uv) || !finite_color(tint) ||
                (sampling != tgfx::DrawTextureSampling2D::Linear && sampling != tgfx::DrawTextureSampling2D::Nearest)) {
                throw std::invalid_argument("invalid ImageItem2D state");
            }
        }

    } // namespace

    ImageItem2D::ImageItem2D()
        : NativeGraphicItem2D("termin.visual.Image2D") {}

    ImageItem2D::ImageItem2D(std::string image_uri,
                             termin::Rect2f rect,
                             termin::Rect2f uv,
                             termin::SrgbColor tint,
                             tgfx::DrawTextureSampling2D sampling)
        : ImageItem2D() {
        validate(image_uri, rect, uv, tint, sampling);
        image_uri_ = std::move(image_uri);
        rect_ = rect;
        uv_ = uv;
        tint_ = tint;
        sampling_ = sampling;
    }

    void ImageItem2D::set_image_uri(std::string uri) {
        validate(uri, rect_, uv_, tint_, sampling_);
        image_uri_ = std::move(uri);
    }

    void ImageItem2D::set_rect(termin::Rect2f rect) {
        validate(image_uri_, rect, uv_, tint_, sampling_);
        rect_ = rect;
    }

    void ImageItem2D::set_uv(termin::Rect2f uv) {
        validate(image_uri_, rect_, uv, tint_, sampling_);
        uv_ = uv;
    }

    void ImageItem2D::set_tint(termin::SrgbColor tint) {
        validate(image_uri_, rect_, uv_, tint, sampling_);
        tint_ = tint;
    }

    void ImageItem2D::set_sampling(tgfx::DrawTextureSampling2D sampling) {
        validate(image_uri_, rect_, uv_, tint_, sampling);
        sampling_ = sampling;
    }

    std::optional<termin::Bounds2f> ImageItem2D::local_bounds() const {
        return detail::rect_bounds(rect_);
    }

    bool ImageItem2D::hit_test(termin::Vec2f point, float) const {
        return detail::rect_contains(rect_, point);
    }

    bool ImageItem2D::paint(GraphicItemPaintContext2D& context) const {
        return context.image(image_uri_, rect_, uv_, tint_, sampling_);
    }

} // namespace termin::visual
