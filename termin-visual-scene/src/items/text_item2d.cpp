#include "termin_visual_scene/items/text_item2d.hpp"

#include "item_geometry2d_internal.hpp"

#include <cmath>
#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        void validate(const std::string& text,
                      const std::string& font_uri,
                      termin::Vec2f origin,
                      float size_px,
                      tgfx::Color4f color,
                      termin::Bounds2f layout_bounds) {
            if (text.empty() || font_uri.empty() || !detail::valid_point(origin) || !std::isfinite(size_px) ||
                size_px <= 0.0f || !color.is_finite() || !detail::valid_bounds(layout_bounds)) {
                throw std::invalid_argument("invalid TextItem2D state");
            }
        }

        const char* anchor_name(tgfx::TextAnchor2D anchor) {
            if (anchor == tgfx::TextAnchor2D::Center) {
                return "center";
            }
            if (anchor == tgfx::TextAnchor2D::Right) {
                return "right";
            }
            return "left";
        }

        tgfx::TextAnchor2D decode_anchor(const std::string& value) {
            if (value == "left")
                return tgfx::TextAnchor2D::Left;
            if (value == "center")
                return tgfx::TextAnchor2D::Center;
            if (value == "right")
                return tgfx::TextAnchor2D::Right;
            throw std::invalid_argument("unknown text anchor");
        }

    } // namespace

    TextItem2D::TextItem2D()
        : NativeGraphicItem2D("termin.visual.Text2D") {}

    TextItem2D::TextItem2D(std::string text,
                           std::string font_uri,
                           termin::Vec2f origin,
                           float size_px,
                           tgfx::Color4f color,
                           tgfx::TextAnchor2D anchor,
                           termin::Bounds2f layout_bounds)
        : TextItem2D() {
        validate(text, font_uri, origin, size_px, color, layout_bounds);
        text_ = std::move(text);
        font_uri_ = std::move(font_uri);
        origin_ = origin;
        size_px_ = size_px;
        color_ = color;
        anchor_ = anchor;
        layout_bounds_ = layout_bounds;
    }

    void TextItem2D::set_text(std::string text) {
        validate(text, font_uri_, origin_, size_px_, color_, layout_bounds_);
        text_ = std::move(text);
    }

    void TextItem2D::set_font_uri(std::string uri) {
        validate(text_, uri, origin_, size_px_, color_, layout_bounds_);
        font_uri_ = std::move(uri);
    }

    void TextItem2D::set_origin(termin::Vec2f origin) {
        validate(text_, font_uri_, origin, size_px_, color_, layout_bounds_);
        origin_ = origin;
    }

    void TextItem2D::set_size_px(float size) {
        validate(text_, font_uri_, origin_, size, color_, layout_bounds_);
        size_px_ = size;
    }

    void TextItem2D::set_color(tgfx::Color4f color) {
        validate(text_, font_uri_, origin_, size_px_, color, layout_bounds_);
        color_ = color;
    }

    void TextItem2D::set_anchor(tgfx::TextAnchor2D anchor) {
        anchor_ = anchor;
    }

    void TextItem2D::set_layout_bounds(termin::Bounds2f bounds) {
        validate(text_, font_uri_, origin_, size_px_, color_, bounds);
        layout_bounds_ = bounds;
    }

    std::optional<termin::Bounds2f> TextItem2D::local_bounds() const {
        return layout_bounds_;
    }

    bool TextItem2D::hit_test(termin::Vec2f point, float) const {
        return detail::bounds_contains(layout_bounds_, point);
    }

    bool TextItem2D::paint(GraphicItemPaintContext2D& context) const {
        return context.text(text_, font_uri_, origin_, size_px_, color_, anchor_);
    }

} // namespace termin::visual
