#include "tgfx2/draw_list2d.hpp"

#include <cmath>

#include <tcbase/tc_log.hpp>

namespace tgfx {
    namespace {
        bool finite_color(const termin::LinearColor& color) {
            return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) && std::isfinite(color.a);
        }
    } // namespace
    namespace {

        bool finite(float value) {
            return std::isfinite(value);
        }

        bool finite(termin::Vec2f value) {
            return finite(value.x) && finite(value.y);
        }

        bool valid_rect(termin::Rect2f rect) {
            return finite(rect.x) && finite(rect.y) && finite(rect.width) && finite(rect.height) &&
                   rect.width >= 0.0f && rect.height >= 0.0f;
        }

        bool valid_rule(FillRule rule) {
            return rule == FillRule::NonZero || rule == FillRule::EvenOdd;
        }

        bool valid_sampling(DrawTextureSampling2D sampling) {
            return sampling == DrawTextureSampling2D::Linear || sampling == DrawTextureSampling2D::Nearest;
        }

        bool valid_anchor(TextAnchor2D anchor) {
            return anchor == TextAnchor2D::Left || anchor == TextAnchor2D::Center || anchor == TextAnchor2D::Right;
        }

    } // namespace

    bool DrawList2DBuilder::pop_scope_(Scope expected, const char* operation) {
        if (scopes_.empty() || scopes_.back() != expected) {
            tc::Log::error("[DrawList2DBuilder] %s has no matching active scope", operation);
            return false;
        }
        scopes_.pop_back();
        return true;
    }

    bool DrawList2DBuilder::append(const DrawList2D& list) {
        try {
            commands_.insert(commands_.end(), list.commands().begin(), list.commands().end());
            return true;
        } catch (const std::exception& error) {
            tc_log_error("[tgfx2] DrawList2D append failed: %s", error.what());
            return false;
        }
    }

    bool DrawList2DBuilder::push_transform(const termin::Affine2f& transform) {
        if (!transform.is_finite()) {
            tc::Log::error("[DrawList2DBuilder] non-finite transform rejected");
            return false;
        }
        commands_.emplace_back(PushTransform2D{transform});
        scopes_.push_back(Scope::Transform);
        return true;
    }

    bool DrawList2DBuilder::pop_transform() {
        if (!pop_scope_(Scope::Transform, "pop_transform"))
            return false;
        commands_.emplace_back(PopTransform2D{});
        return true;
    }

    bool DrawList2DBuilder::push_opacity(float opacity) {
        if (!finite(opacity) || opacity < 0.0f || opacity > 1.0f) {
            tc::Log::error("[DrawList2DBuilder] opacity outside [0, 1] rejected");
            return false;
        }
        commands_.emplace_back(PushOpacity2D{opacity});
        scopes_.push_back(Scope::Opacity);
        return true;
    }

    bool DrawList2DBuilder::pop_opacity() {
        if (!pop_scope_(Scope::Opacity, "pop_opacity"))
            return false;
        commands_.emplace_back(PopOpacity2D{});
        return true;
    }

    bool DrawList2DBuilder::push_clip(Path2f path, FillRule rule) {
        if (path.empty() || !valid_rule(rule)) {
            tc::Log::error("[DrawList2DBuilder] empty or invalid geometric clip rejected");
            return false;
        }
        commands_.emplace_back(PushClip2D{std::move(path), rule});
        scopes_.push_back(Scope::Clip);
        return true;
    }

    bool DrawList2DBuilder::push_clip_rect(termin::Rect2f rect) {
        if (!valid_rect(rect) || rect.width <= 0.0f || rect.height <= 0.0f) {
            tc::Log::error("[DrawList2DBuilder] invalid clip rectangle rejected");
            return false;
        }
        Path2f path;
        if (!path.move_to({rect.x, rect.y}) || !path.line_to({rect.x + rect.width, rect.y}) ||
            !path.line_to({rect.x + rect.width, rect.y + rect.height}) ||
            !path.line_to({rect.x, rect.y + rect.height}) || !path.close()) {
            tc::Log::error("[DrawList2DBuilder] failed to build clip rectangle");
            return false;
        }
        return push_clip(std::move(path));
    }

    bool DrawList2DBuilder::pop_clip() {
        if (!pop_scope_(Scope::Clip, "pop_clip"))
            return false;
        commands_.emplace_back(PopClip2D{});
        return true;
    }

    bool DrawList2DBuilder::rect(termin::Rect2f rect, FillPaint paint) {
        if (!valid_rect(rect) || rect.width <= 0.0f || rect.height <= 0.0f || !paint.validate()) {
            tc::Log::error("[DrawList2DBuilder] invalid rectangle rejected");
            return false;
        }
        commands_.emplace_back(DrawRect2D{rect, paint});
        return true;
    }

    bool DrawList2DBuilder::rounded_rect(termin::Rect2f rect,
                                         float radius,
                                         FillPaint paint,
                                         std::optional<StrokePaint> stroke) {
        if (!valid_rect(rect) || rect.width <= 0.0f || rect.height <= 0.0f || !finite(radius) || radius < 0.0f ||
            !paint.validate() || (stroke && !stroke->validate())) {
            tc::Log::error("[DrawList2DBuilder] invalid rounded rectangle rejected");
            return false;
        }
        commands_.emplace_back(DrawRoundedRect2D{rect, radius, paint, std::move(stroke)});
        return true;
    }

    bool DrawList2DBuilder::ellipse(termin::Rect2f bounds, FillPaint paint, std::optional<StrokePaint> stroke) {
        if (!valid_rect(bounds) || bounds.width <= 0.0f || bounds.height <= 0.0f || !paint.validate() ||
            (stroke && !stroke->validate())) {
            tc::Log::error("[DrawList2DBuilder] invalid ellipse rejected");
            return false;
        }
        commands_.emplace_back(DrawEllipse2D{bounds, paint, std::move(stroke)});
        return true;
    }

    bool DrawList2DBuilder::path(Path2f path, std::optional<FillPaint> fill, std::optional<StrokePaint> stroke) {
        if (path.empty() || (!fill && !stroke) || (fill && !fill->validate()) || (stroke && !stroke->validate())) {
            tc::Log::error("[DrawList2DBuilder] invalid path draw rejected");
            return false;
        }
        commands_.emplace_back(DrawPath2D{std::move(path), std::move(fill), std::move(stroke)});
        return true;
    }

    bool DrawList2DBuilder::polyline(std::span<const termin::Vec2f> points, StrokePaint stroke, bool closed) {
        if (points.size() < 2 || !stroke.validate()) {
            tc::Log::error("[DrawList2DBuilder] invalid polyline rejected");
            return false;
        }
        for (const auto point : points) {
            if (!finite(point)) {
                tc::Log::error("[DrawList2DBuilder] non-finite polyline rejected");
                return false;
            }
        }
        commands_.emplace_back(
            DrawPolyline2D{std::vector<termin::Vec2f>(points.begin(), points.end()), std::move(stroke), closed});
        return true;
    }

    bool DrawList2DBuilder::text(std::string text_value,
                                 termin::Vec2f origin,
                                 float size_px,
                                 termin::LinearColor color,
                                 FontHandle font,
                                 TextAnchor2D anchor,
                                 std::optional<float> coverage_gamma) {
        if (text_value.empty() || !finite(origin) || !finite(size_px) || size_px <= 0.0f || !finite_color(color) ||
            !font || !valid_anchor(anchor) ||
            (coverage_gamma.has_value() && (!finite(*coverage_gamma) || *coverage_gamma <= 0.0f))) {
            tc::Log::error("[DrawList2DBuilder] invalid text draw rejected");
            return false;
        }
        commands_.emplace_back(DrawText2D{std::move(text_value), origin, size_px, color, font, anchor, coverage_gamma});
        return true;
    }

    bool DrawList2DBuilder::image(TextureHandle texture,
                                  termin::Rect2f rect,
                                  termin::Rect2f uv,
                                  termin::LinearColor tint,
                                  DrawTextureSampling2D sampling) {
        if (!texture || !valid_rect(rect) || rect.width <= 0.0f || rect.height <= 0.0f || !valid_rect(uv) ||
            !finite_color(tint) || !valid_sampling(sampling)) {
            tc::Log::error("[DrawList2DBuilder] invalid image draw rejected");
            return false;
        }
        commands_.emplace_back(DrawImage2D{texture, rect, uv, tint, sampling});
        return true;
    }

    bool DrawList2DBuilder::custom_batch(std::span<const DrawVertex2D> vertices,
                                         termin::LinearColor color,
                                         TextureHandle texture,
                                         DrawTextureSampling2D sampling) {
        if (vertices.empty() || vertices.size() % 3 != 0 || !finite_color(color) || !valid_sampling(sampling)) {
            tc::Log::error("[DrawList2DBuilder] invalid custom triangle batch rejected");
            return false;
        }
        for (const auto& vertex : vertices) {
            if (!finite(vertex.position) || !finite(vertex.uv)) {
                tc::Log::error("[DrawList2DBuilder] non-finite custom batch rejected");
                return false;
            }
        }
        commands_.emplace_back(
            DrawCustomBatch2D{std::vector<DrawVertex2D>(vertices.begin(), vertices.end()), color, texture, sampling});
        return true;
    }

    bool DrawList2DBuilder::retained_batch(std::shared_ptr<RetainedDrawBatch2D> batch) {
        if (!batch) {
            tc::Log::error("[DrawList2DBuilder] null retained batch rejected");
            return false;
        }
        commands_.emplace_back(DrawRetainedBatch2D{std::move(batch)});
        return true;
    }

    std::optional<DrawList2D> DrawList2DBuilder::freeze() {
        if (!scopes_.empty()) {
            tc::Log::error("[DrawList2DBuilder] cannot freeze unbalanced state scopes");
            return std::nullopt;
        }
        DrawList2D result(std::move(commands_));
        commands_.clear();
        return result;
    }

    void DrawList2DBuilder::recycle(DrawList2D&& list) noexcept {
        commands_ = std::move(list.commands_);
        commands_.clear();
        scopes_.clear();
    }

    void DrawList2DBuilder::clear() noexcept {
        commands_.clear();
        scopes_.clear();
    }

} // namespace tgfx
