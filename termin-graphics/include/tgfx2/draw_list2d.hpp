// Immutable backend-neutral 2D draw command vocabulary.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <termin/geom/affine2.hpp>
#include <termin/geom/rect2.hpp>
#include <termin/geom/vec2.hpp>

#include "tgfx2/handles.hpp"
#include "tgfx2/path2d.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

    class FontAtlas;
    class RenderContext2;

    enum class DrawTextureSampling2D : std::uint8_t {
        Linear,
        Nearest,
    };

    enum class TextAnchor2D : std::uint8_t {
        Left,
        Center,
        Right,
    };

    struct PushTransform2D {
        termin::Affine2f transform = termin::Affine2f::identity();
    };
    struct PopTransform2D {};

    struct PushOpacity2D {
        float opacity = 1.0f;
    };
    struct PopOpacity2D {};

    // This is transformed fill geometry, never a device-space scissor. The Canvas
    // executor may select a scissor only when the effective geometry is exactly an
    // axis-aligned rectangle.
    struct PushClip2D {
        Path2f path;
        FillRule rule = FillRule::NonZero;
    };
    struct PopClip2D {};

    struct DrawRect2D {
        termin::Rect2f rect{};
        FillPaint paint{};
    };

    struct DrawRoundedRect2D {
        termin::Rect2f rect{};
        float radius = 0.0f;
        FillPaint paint{};
        std::optional<StrokePaint> stroke;
    };

    struct DrawEllipse2D {
        termin::Rect2f bounds{};
        FillPaint paint{};
        std::optional<StrokePaint> stroke;
    };

    struct DrawPath2D {
        Path2f path;
        std::optional<FillPaint> fill;
        std::optional<StrokePaint> stroke;
    };

    struct DrawPolyline2D {
        std::vector<termin::Vec2f> points;
        StrokePaint stroke{};
        bool closed = false;
    };

    struct DrawText2D {
        std::string text;
        termin::Vec2f origin{};
        float size_px = 14.0f;
        termin::LinearColor color{1.0f, 1.0f, 1.0f, 1.0f};
        FontHandle font{};
        TextAnchor2D anchor = TextAnchor2D::Left;
        std::optional<float> coverage_gamma = std::nullopt;
    };

    struct DrawImage2D {
        TextureHandle texture{};
        termin::Rect2f rect{};
        termin::Rect2f uv{0.0f, 0.0f, 1.0f, 1.0f};
        termin::LinearColor tint{1.0f, 1.0f, 1.0f, 1.0f};
        DrawTextureSampling2D sampling = DrawTextureSampling2D::Linear;
    };

    struct DrawVertex2D {
        termin::Vec2f position{};
        termin::Vec2f uv{};
    };

    // An owned triangle batch for specialized producers. A zero texture selects
    // the solid Canvas shader; a non-zero texture selects the standard image
    // shader. Persistent asset identity must already have been resolved.
    struct DrawCustomBatch2D {
        std::vector<DrawVertex2D> vertices;
        termin::LinearColor color{1.0f, 1.0f, 1.0f, 1.0f};
        TextureHandle texture{};
        DrawTextureSampling2D sampling = DrawTextureSampling2D::Linear;
    };

    // State accumulated by the backend-neutral command stream before a retained
    // native batch is executed. Native batches are intended for large,
    // renderer-owned GPU streams which must not be copied into DrawList2D every
    // frame. They still receive the scene transform, opacity and rectangular clip
    // explicitly and render only through the backend-neutral RenderContext2 API.
    struct RetainedDrawState2D {
        termin::Affine2f transform = termin::Affine2f::identity();
        float opacity = 1.0f;
        termin::Rect2f clip_rect{};
        bool has_clip_rect = false;
        bool unsupported_clip = false;
        int viewport_x = 0;
        int viewport_y = 0;
        int viewport_width = 0;
        int viewport_height = 0;
    };

    class TGFX2_TYPE_API RetainedDrawBatch2D {
    public:
        virtual ~RetainedDrawBatch2D() = default;
        virtual bool draw(RenderContext2& context, const RetainedDrawState2D& state) = 0;
    };

    struct DrawRetainedBatch2D {
        std::shared_ptr<RetainedDrawBatch2D> batch;
    };

    using DrawCommand2D = std::variant<PushTransform2D,
                                       PopTransform2D,
                                       PushOpacity2D,
                                       PopOpacity2D,
                                       PushClip2D,
                                       PopClip2D,
                                       DrawRect2D,
                                       DrawRoundedRect2D,
                                       DrawEllipse2D,
                                       DrawPath2D,
                                       DrawPolyline2D,
                                       DrawText2D,
                                       DrawImage2D,
                                       DrawCustomBatch2D,
                                       DrawRetainedBatch2D>;

    class DrawList2DBuilder;

    // DrawList2D has no mutation surface. It owns all strings, paths, point arrays
    // and custom vertices and shares retained batch bodies, so it remains valid
    // after its builder is reused or destroyed. Runtime resource handles remain
    // borrowed from their device or resolver and must be live for execute().
    class TGFX2_TYPE_API DrawList2D {
    public:
        DrawList2D() = default;

        const std::vector<DrawCommand2D>& commands() const noexcept {
            return commands_;
        }
        bool empty() const noexcept {
            return commands_.empty();
        }
        std::size_t size() const noexcept {
            return commands_.size();
        }

    private:
        explicit DrawList2D(std::vector<DrawCommand2D>&& commands)
            : commands_(std::move(commands)) {}

        std::vector<DrawCommand2D> commands_;
        friend class DrawList2DBuilder;
    };

    class TGFX2_TYPE_API DrawList2DBuilder {
    public:
        // Deep-copies an already frozen command stream into the current builder.
        // Scope balance is validated by the source freeze operation, so the
        // appended stream cannot disturb the destination scope stack.
        bool append(const DrawList2D& list);
        bool push_transform(const termin::Affine2f& transform);
        bool pop_transform();
        bool push_opacity(float opacity);
        bool pop_opacity();
        bool push_clip(Path2f path, FillRule rule = FillRule::NonZero);
        bool push_clip_rect(termin::Rect2f rect);
        bool pop_clip();

        bool rect(termin::Rect2f rect, FillPaint paint);
        bool rounded_rect(termin::Rect2f rect,
                          float radius,
                          FillPaint paint,
                          std::optional<StrokePaint> stroke = std::nullopt);
        bool ellipse(termin::Rect2f bounds, FillPaint paint, std::optional<StrokePaint> stroke = std::nullopt);
        bool path(Path2f path, std::optional<FillPaint> fill, std::optional<StrokePaint> stroke = std::nullopt);
        bool polyline(std::span<const termin::Vec2f> points, StrokePaint stroke, bool closed = false);
        bool text(std::string text,
                  termin::Vec2f origin,
                  float size_px,
                  termin::LinearColor color,
                  FontHandle font,
                  TextAnchor2D anchor = TextAnchor2D::Left,
                  std::optional<float> coverage_gamma = std::nullopt);
        bool image(TextureHandle texture,
                   termin::Rect2f rect,
                   termin::Rect2f uv = {0.0f, 0.0f, 1.0f, 1.0f},
                   termin::LinearColor tint = termin::LinearColor{1.0f, 1.0f, 1.0f, 1.0f},
                   DrawTextureSampling2D sampling = DrawTextureSampling2D::Linear);
        bool custom_batch(std::span<const DrawVertex2D> vertices,
                          termin::LinearColor color,
                          TextureHandle texture = {},
                          DrawTextureSampling2D sampling = DrawTextureSampling2D::Linear);
        bool retained_batch(std::shared_ptr<RetainedDrawBatch2D> batch);

        // Fails without consuming the builder if any state scope is unbalanced.
        std::optional<DrawList2D> freeze();
        // Reclaims a consumed list's command storage for the next build. Call only
        // after the list has finished executing.
        void recycle(DrawList2D&& list) noexcept;
        void clear() noexcept;

    private:
        enum class Scope : std::uint8_t {
            Transform,
            Opacity,
            Clip
        };
        bool pop_scope_(Scope expected, const char* operation);
        std::vector<DrawCommand2D> commands_;
        std::vector<Scope> scopes_;
    };

    class TGFX2_TYPE_API DrawResourceResolver2D {
    public:
        virtual ~DrawResourceResolver2D() = default;
        virtual FontAtlas* resolve_font(FontHandle handle) = 0;
    };

} // namespace tgfx
