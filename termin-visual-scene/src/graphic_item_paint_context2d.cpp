#include "termin_visual_scene/graphic_item_paint_context2d.hpp"

#include "graphic_item_draw_sink_internal.hpp"

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {
        termin::LinearColor to_gpu_color(termin::SrgbColor color) {
            const auto linear = termin::srgb_to_linear(color);
            return {linear.r, linear.g, linear.b, linear.a};
        }
    } // namespace

    bool GraphicItemPaintContext2D::rect(termin::Rect2f rect, tgfx::FillPaint fill) {
        return sink_ != nullptr && sink_->builder != nullptr && sink_->builder->rect(rect, fill);
    }

    bool GraphicItemPaintContext2D::rounded_rect(termin::Rect2f rect,
                                                 float radius,
                                                 tgfx::FillPaint fill,
                                                 std::optional<tgfx::StrokePaint> stroke) {
        return sink_ != nullptr && sink_->builder != nullptr &&
               sink_->builder->rounded_rect(rect, radius, fill, std::move(stroke));
    }

    bool GraphicItemPaintContext2D::ellipse(termin::Rect2f bounds,
                                            tgfx::FillPaint fill,
                                            std::optional<tgfx::StrokePaint> stroke) {
        return sink_ != nullptr && sink_->builder != nullptr &&
               sink_->builder->ellipse(bounds, fill, std::move(stroke));
    }

    bool GraphicItemPaintContext2D::path(tgfx::Path2f path,
                                         std::optional<tgfx::FillPaint> fill,
                                         std::optional<tgfx::StrokePaint> stroke) {
        return sink_ != nullptr && sink_->builder != nullptr &&
               sink_->builder->path(std::move(path), std::move(fill), std::move(stroke));
    }

    bool
    GraphicItemPaintContext2D::polyline(std::span<const termin::Vec2f> points, tgfx::StrokePaint stroke, bool closed) {
        return sink_ != nullptr && sink_->builder != nullptr &&
               sink_->builder->polyline(points, std::move(stroke), closed);
    }

    bool GraphicItemPaintContext2D::text(std::string text,
                                         std::string font_uri,
                                         termin::Vec2f origin,
                                         float size_px,
                                         termin::SrgbColor color,
                                         tgfx::TextAnchor2D anchor,
                                         std::optional<float> coverage_gamma) {
        if (sink_ == nullptr || sink_->builder == nullptr || sink_->resolver == nullptr) {
            return false;
        }
        const auto font = sink_->resolver->resolve_font(font_uri);
        if (!font || !*font) {
            tc::Log::error("graphic item font '%s' was not resolved", font_uri.c_str());
            return false;
        }
        return sink_->builder->text(
            std::move(text), origin, size_px, to_gpu_color(color), *font, anchor, coverage_gamma);
    }

    bool GraphicItemPaintContext2D::image(std::string image_uri,
                                          termin::Rect2f rect,
                                          termin::Rect2f uv,
                                          termin::SrgbColor tint,
                                          tgfx::DrawTextureSampling2D sampling) {
        if (sink_ == nullptr || sink_->builder == nullptr || sink_->resolver == nullptr) {
            return false;
        }
        const auto image = sink_->resolver->resolve_image(image_uri);
        if (!image || !*image) {
            tc::Log::error("graphic item image '%s' was not resolved", image_uri.c_str());
            return false;
        }
        return sink_->builder->image(*image, rect, uv, to_gpu_color(tint), sampling);
    }

    bool GraphicItemPaintContext2D::custom_batch(std::string key, termin::Bounds2f local_bounds) {
        if (sink_ == nullptr || sink_->builder == nullptr || sink_->resolver == nullptr) {
            return false;
        }
        const auto batch = sink_->resolver->resolve_custom_batch(key, local_bounds);
        if (!batch) {
            tc::Log::error("graphic item custom batch '%s' was not resolved", key.c_str());
            return false;
        }
        return sink_->builder->custom_batch(batch->vertices, batch->color, batch->texture, batch->sampling);
    }

    bool GraphicItemPaintContext2D::retained_batch(std::shared_ptr<tgfx::RetainedDrawBatch2D> batch) {
        return sink_ != nullptr && sink_->builder != nullptr && sink_->builder->retained_batch(std::move(batch));
    }

    bool GraphicItemPaintContext2D::push_clip_rect(termin::Rect2f rect) {
        if (sink_ == nullptr || sink_->composition == nullptr) {
            return false;
        }
        tgfx::Path2f path;
        if (!path.move_to({rect.x, rect.y}) || !path.line_to({rect.x + rect.width, rect.y}) ||
            !path.line_to({rect.x + rect.width, rect.y + rect.height}) ||
            !path.line_to({rect.x, rect.y + rect.height}) || !path.close()) {
            tc::Log::error("graphic item rejected invalid local clip rectangle");
            return false;
        }
        tgfx::CompositionLayer2D layer;
        layer.clip = tgfx::CompositionClip2D{std::move(path), tgfx::FillRule::NonZero};
        return sink_->composition->push(layer);
    }

    bool GraphicItemPaintContext2D::pop_clip() {
        return sink_ != nullptr && sink_->composition != nullptr && sink_->composition->pop();
    }

} // namespace termin::visual
