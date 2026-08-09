// canvas2d_renderer.cpp - Reusable immediate 2D drawing facade for tgfx2.

#include "tgfx2/canvas2d_renderer.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <numeric>
#include <type_traits>

#include "internal/utf8_decode.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include <termin/geom/color.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_shader_registry.h>
}

#include <tcbase/tc_log.hpp>

namespace tgfx {

    namespace {

        struct CanvasPushData {
            float projection[16];
            float color[4];
        };
        static_assert(sizeof(CanvasPushData) == 80, "CanvasPushData layout drift - shader and C++ disagree");

        constexpr const char* CANVAS2D_SOLID_SHADER_UUID = "termin-engine-canvas2d-solid";
        constexpr const char* CANVAS2D_TEXTURE_SHADER_UUID = "termin-engine-canvas2d-texture";

        tc_shader_handle solid_shader_handle() {
            static tc_shader_handle handle = tc_shader_handle_invalid();
            if (!tc_shader_is_valid(handle)) {
                handle = register_builtin_shader_from_catalog(CANVAS2D_SOLID_SHADER_UUID);
            }
            return handle;
        }

        tc_shader_handle texture_shader_handle() {
            static tc_shader_handle handle = tc_shader_handle_invalid();
            if (!tc_shader_is_valid(handle)) {
                handle = register_builtin_shader_from_catalog(CANVAS2D_TEXTURE_SHADER_UUID);
            }
            return handle;
        }

        void build_ortho_pixel_to_ndc(float x, float y, float w, float h, float out[16]) {
            if (w <= 0.0f || h <= 0.0f) {
                std::memset(out, 0, 16 * sizeof(float));
                out[0] = out[5] = out[10] = out[15] = 1.0f;
                return;
            }

            // Row-major math matrix, then transposed to column-major storage
            // for push constants. Pixel coords are absolute in the current
            // render target; viewport origin is accounted for by the constant.
            // This builds canonical TerminClip: pixel top-left maps to (-1, -1).
            float rm[16]{};
            rm[0] = 2.0f / w;
            rm[3] = -1.0f - 2.0f * x / w;
            rm[5] = 2.0f / h;
            rm[7] = -1.0f - 2.0f * y / h;
            rm[10] = 1.0f;
            rm[15] = 1.0f;
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    out[col * 4 + row] = rm[row * 4 + col];
                }
            }
        }

        bool same_color(termin::LinearColor a, termin::LinearColor b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        using DrawTriangle2D = std::array<DrawVertex2D, 3>;
        using ClipMesh2D = std::vector<DrawTriangle2D>;

        struct NativeClip2D {
            termin::Rect2f rect{};
            bool has_rect = false;
            bool unsupported = false;
        };

        NativeClip2D retained_clip(const Path2f& path, const termin::Affine2f& transform, const NativeClip2D& parent) {
            NativeClip2D result = parent;
            if (parent.unsupported)
                return result;
            if (path.points().size() != 4) {
                result.unsupported = true;
                return result;
            }
            std::array<termin::Vec2f, 4> points{};
            for (std::size_t index = 0; index < points.size(); ++index) {
                points[index] = transform.transform_point(path.points()[index]);
            }
            float left = points[0].x;
            float right = points[0].x;
            float top = points[0].y;
            float bottom = points[0].y;
            for (const auto point : points) {
                left = std::min(left, point.x);
                right = std::max(right, point.x);
                top = std::min(top, point.y);
                bottom = std::max(bottom, point.y);
            }
            constexpr float epsilon = 1.0e-4f;
            for (const auto point : points) {
                const bool x_edge = std::fabs(point.x - left) <= epsilon || std::fabs(point.x - right) <= epsilon;
                const bool y_edge = std::fabs(point.y - top) <= epsilon || std::fabs(point.y - bottom) <= epsilon;
                if (!x_edge || !y_edge) {
                    result.unsupported = true;
                    return result;
                }
            }
            termin::Rect2f rect{left, top, right - left, bottom - top};
            if (rect.width <= 0.0f || rect.height <= 0.0f) {
                result.unsupported = true;
                return result;
            }
            if (result.has_rect) {
                const float x0 = std::max(result.rect.x, rect.x);
                const float y0 = std::max(result.rect.y, rect.y);
                const float x1 = std::min(result.rect.x + result.rect.width, rect.x + rect.width);
                const float y1 = std::min(result.rect.y + result.rect.height, rect.y + rect.height);
                result.rect = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
            } else {
                result.rect = rect;
                result.has_rect = true;
            }
            return result;
        }

        float cross(termin::Vec2f a, termin::Vec2f b, termin::Vec2f c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        }

        float signed_area(std::span<const termin::Vec2f> points) {
            float result = 0.0f;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto a = points[i];
                const auto b = points[(i + 1) % points.size()];
                result += a.x * b.y - b.x * a.y;
            }
            return result * 0.5f;
        }

        bool
        point_in_triangle(termin::Vec2f point, termin::Vec2f a, termin::Vec2f b, termin::Vec2f c, float orientation) {
            constexpr float epsilon = 1.0e-5f;
            return orientation * cross(a, b, point) >= -epsilon && orientation * cross(b, c, point) >= -epsilon &&
                   orientation * cross(c, a, point) >= -epsilon;
        }

        bool triangulate_contour(std::span<const termin::Vec2f> input, std::vector<DrawTriangle2D>& output) {
            std::vector<termin::Vec2f> points(input.begin(), input.end());
            if (points.size() > 1 && points.front().x == points.back().x && points.front().y == points.back().y) {
                points.pop_back();
            }
            if (points.size() < 3)
                return false;

            const float area = signed_area(points);
            if (!std::isfinite(area) || std::fabs(area) <= 1.0e-6f) {
                tc::Log::error("[Canvas2DRenderer] degenerate contour rejected");
                return false;
            }
            const float orientation = area > 0.0f ? 1.0f : -1.0f;
            std::vector<std::size_t> indices(points.size());
            std::iota(indices.begin(), indices.end(), 0);

            std::size_t guard = points.size() * points.size();
            while (indices.size() > 3 && guard-- > 0) {
                bool removed = false;
                for (std::size_t i = 0; i < indices.size(); ++i) {
                    const std::size_t ia = indices[(i + indices.size() - 1) % indices.size()];
                    const std::size_t ib = indices[i];
                    const std::size_t ic = indices[(i + 1) % indices.size()];
                    const auto a = points[ia];
                    const auto b = points[ib];
                    const auto c = points[ic];
                    if (orientation * cross(a, b, c) <= 1.0e-6f)
                        continue;

                    bool contains = false;
                    for (const auto index : indices) {
                        if (index == ia || index == ib || index == ic)
                            continue;
                        if (point_in_triangle(points[index], a, b, c, orientation)) {
                            contains = true;
                            break;
                        }
                    }
                    if (contains)
                        continue;

                    output.push_back(DrawTriangle2D{{{a, {}}, {b, {}}, {c, {}}}});
                    indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
                    removed = true;
                    break;
                }
                if (!removed) {
                    tc::Log::error("[Canvas2DRenderer] non-simple contour cannot be tessellated");
                    return false;
                }
            }
            if (indices.size() != 3)
                return false;
            output.push_back(
                DrawTriangle2D{{{points[indices[0]], {}}, {points[indices[1]], {}}, {points[indices[2]], {}}}});
            return true;
        }

        float edge_x_at(termin::Vec2f a, termin::Vec2f b, float y) {
            return a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
        }

        bool tessellate_path(const Path2f& path, const termin::Affine2f& transform, FillRule rule, ClipMesh2D& output) {
            const auto flattened = path.flatten(0.2f, transform);
            std::vector<float> levels;
            for (const auto& contour : flattened.contours) {
                if (!contour.closed || contour.points.size() < 3)
                    continue;
                for (const auto point : contour.points)
                    levels.push_back(point.y);
            }
            std::sort(levels.begin(), levels.end());
            levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

            struct Crossing {
                float x = 0.0f;
                termin::Vec2f a{};
                termin::Vec2f b{};
            };
            for (std::size_t level = 1; level < levels.size(); ++level) {
                const float y0 = levels[level - 1];
                const float y1 = levels[level];
                if (y1 - y0 <= 1.0e-6f)
                    continue;
                const float middle_y = (y0 + y1) * 0.5f;
                std::vector<Crossing> crossings;
                for (const auto& contour : flattened.contours) {
                    if (!contour.closed || contour.points.size() < 3)
                        continue;
                    for (std::size_t i = 0; i < contour.points.size(); ++i) {
                        const auto a = contour.points[i];
                        const auto b = contour.points[(i + 1) % contour.points.size()];
                        if (std::fabs(a.y - b.y) <= 1.0e-8f)
                            continue;
                        if (middle_y <= std::min(a.y, b.y) || middle_y >= std::max(a.y, b.y)) {
                            continue;
                        }
                        crossings.push_back({edge_x_at(a, b, middle_y), a, b});
                    }
                }
                std::sort(
                    crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
                for (std::size_t i = 1; i < crossings.size(); ++i) {
                    const auto& left = crossings[i - 1];
                    const auto& right = crossings[i];
                    if (right.x - left.x <= 1.0e-6f ||
                        !flattened.contains({(left.x + right.x) * 0.5f, middle_y}, rule)) {
                        continue;
                    }
                    const DrawVertex2D left0{{edge_x_at(left.a, left.b, y0), y0}, {}};
                    const DrawVertex2D left1{{edge_x_at(left.a, left.b, y1), y1}, {}};
                    const DrawVertex2D right0{{edge_x_at(right.a, right.b, y0), y0}, {}};
                    const DrawVertex2D right1{{edge_x_at(right.a, right.b, y1), y1}, {}};
                    output.push_back({{left0, left1, right1}});
                    output.push_back({{left0, right1, right0}});
                }
            }
            if (output.empty()) {
                tc::Log::error("[Canvas2DRenderer] path has no closed fill contour");
                return false;
            }
            return true;
        }

        DrawVertex2D interpolate(const DrawVertex2D& a, const DrawVertex2D& b, float t) {
            return {
                {a.position.x + (b.position.x - a.position.x) * t, a.position.y + (b.position.y - a.position.y) * t},
                {a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t}};
        }

        std::vector<DrawVertex2D> clip_polygon_to_triangle(std::span<const DrawVertex2D> polygon,
                                                           const DrawTriangle2D& clip) {
            std::vector<DrawVertex2D> current(polygon.begin(), polygon.end());
            const float area = cross(clip[0].position, clip[1].position, clip[2].position);
            if (std::fabs(area) <= 1.0e-6f)
                return {};
            const float orientation = area > 0.0f ? 1.0f : -1.0f;

            for (int edge = 0; edge < 3; ++edge) {
                const auto edge_a = clip[edge].position;
                const auto edge_b = clip[(edge + 1) % 3].position;
                std::vector<DrawVertex2D> next;
                if (current.empty())
                    break;
                DrawVertex2D previous = current.back();
                float previous_distance = orientation * cross(edge_a, edge_b, previous.position);
                for (const auto& vertex : current) {
                    const float distance = orientation * cross(edge_a, edge_b, vertex.position);
                    const bool inside = distance >= -1.0e-5f;
                    const bool previous_inside = previous_distance >= -1.0e-5f;
                    if (inside != previous_inside) {
                        const float denominator = previous_distance - distance;
                        if (std::fabs(denominator) > 1.0e-8f) {
                            next.push_back(interpolate(previous, vertex, previous_distance / denominator));
                        }
                    }
                    if (inside)
                        next.push_back(vertex);
                    previous = vertex;
                    previous_distance = distance;
                }
                current = std::move(next);
            }
            return current;
        }

        std::vector<DrawTriangle2D> apply_clips(DrawTriangle2D input, const std::vector<ClipMesh2D>& clips) {
            std::vector<DrawTriangle2D> fragments{input};
            for (const auto& clip_mesh : clips) {
                std::vector<DrawTriangle2D> next;
                for (const auto& fragment : fragments) {
                    for (const auto& clip : clip_mesh) {
                        const auto polygon = clip_polygon_to_triangle(fragment, clip);
                        for (std::size_t i = 2; i < polygon.size(); ++i) {
                            next.push_back(DrawTriangle2D{{polygon[0], polygon[i - 1], polygon[i]}});
                        }
                    }
                }
                fragments = std::move(next);
                if (fragments.empty())
                    break;
            }
            return fragments;
        }

        std::vector<DrawVertex2D> flatten_and_clip(std::span<const DrawTriangle2D> triangles,
                                                   const std::vector<ClipMesh2D>& clips) {
            std::vector<DrawVertex2D> result;
            for (const auto& triangle : triangles) {
                const auto fragments = apply_clips(triangle, clips);
                for (const auto& fragment : fragments) {
                    result.insert(result.end(), fragment.begin(), fragment.end());
                }
            }
            return result;
        }

        termin::LinearColor with_opacity(termin::LinearColor color, float opacity) {
            color.a *= opacity;
            return color;
        }

        std::vector<termin::Vec2f> rect_contour(termin::Rect2f rect) {
            return {{rect.x, rect.y},
                    {rect.x + rect.width, rect.y},
                    {rect.x + rect.width, rect.y + rect.height},
                    {rect.x, rect.y + rect.height}};
        }

        std::vector<termin::Vec2f> ellipse_contour(termin::Rect2f bounds, int segments = 48) {
            constexpr float tau = 6.2831853071795864769f;
            std::vector<termin::Vec2f> result;
            result.reserve(static_cast<std::size_t>(segments));
            const float rx = bounds.width * 0.5f;
            const float ry = bounds.height * 0.5f;
            const float cx = bounds.x + rx;
            const float cy = bounds.y + ry;
            for (int i = 0; i < segments; ++i) {
                const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
                result.push_back({cx + std::cos(angle) * rx, cy + std::sin(angle) * ry});
            }
            return result;
        }

        std::vector<termin::Vec2f> rounded_rect_contour(termin::Rect2f rect, float radius) {
            constexpr float pi = 3.14159265358979323846f;
            constexpr int segments = 8;
            radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) * 0.5f);
            if (radius <= 0.0f)
                return rect_contour(rect);
            std::vector<termin::Vec2f> result;
            result.reserve(4 * (segments + 1));
            const auto corner = [&](float cx, float cy, float start) {
                for (int i = 0; i <= segments; ++i) {
                    const float angle = start + pi * 0.5f * static_cast<float>(i) / static_cast<float>(segments);
                    result.push_back({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius});
                }
            };
            corner(rect.x + radius, rect.y + radius, pi);
            corner(rect.x + rect.width - radius, rect.y + radius, pi * 1.5f);
            corner(rect.x + rect.width - radius, rect.y + rect.height - radius, 0.0f);
            corner(rect.x + radius, rect.y + rect.height - radius, pi * 0.5f);
            return result;
        }

        bool fill_contour(std::span<const termin::Vec2f> local_points,
                          const termin::Affine2f& transform,
                          ClipMesh2D& triangles) {
            std::vector<termin::Vec2f> transformed;
            transformed.reserve(local_points.size());
            for (const auto point : local_points) {
                transformed.push_back(transform.transform_point(point));
            }
            return triangulate_contour(transformed, triangles);
        }

        ClipMesh2D stroke_contour(std::span<const termin::Vec2f> points,
                                  bool closed,
                                  float width,
                                  const termin::Affine2f& transform) {
            ClipMesh2D result;
            if (points.size() < 2 || width <= 0.0f)
                return result;
            const std::size_t segment_count = closed ? points.size() : points.size() - 1;
            for (std::size_t i = 0; i < segment_count; ++i) {
                const auto a = points[i];
                const auto b = points[(i + 1) % points.size()];
                const float dx = b.x - a.x;
                const float dy = b.y - a.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length <= 1.0e-6f)
                    continue;
                const float nx = -dy / length * width * 0.5f;
                const float ny = dx / length * width * 0.5f;
                DrawVertex2D v0{transform.transform_point({a.x + nx, a.y + ny}), {}};
                DrawVertex2D v1{transform.transform_point({a.x - nx, a.y - ny}), {}};
                DrawVertex2D v2{transform.transform_point({b.x - nx, b.y - ny}), {}};
                DrawVertex2D v3{transform.transform_point({b.x + nx, b.y + ny}), {}};
                result.push_back({{v0, v1, v2}});
                result.push_back({{v0, v2, v3}});
            }
            return result;
        }

        CanvasTextureSampling sampling_from(DrawTextureSampling2D sampling) {
            return sampling;
        }

    } // namespace

    Canvas2DRenderer::Canvas2DRenderer(FontAtlas* default_font)
        : default_font_(default_font),
          text2d_(default_font) {}

    Canvas2DRenderer::~Canvas2DRenderer() {
        release_gpu();
    }

    void Canvas2DRenderer::begin(RenderContext2& ctx, int width, int height) {
        begin(ctx, 0, 0, width, height);
    }

    void Canvas2DRenderer::begin(RenderContext2& ctx, int x, int y, int width, int height) {
        ctx_ = &ctx;
        viewport_x_ = x;
        viewport_y_ = y;
        viewport_w_ = width;
        viewport_h_ = height;
        clip_stack_.clear();
        batch_vertices_.clear();
        batch_mode_ = BatchMode::None;
        batch_texture_ = TextureHandle{};
        batch_texture_sampling_ = CanvasTextureSampling::Linear;
        batch_failed_ = false;

        ensure_shaders_(ctx.device());
        ensure_samplers_(ctx.device());
        build_projection_();

        ctx.set_viewport(viewport_x_, viewport_y_, viewport_w_, viewport_h_);
        ctx.set_depth_test(false);
        ctx.set_depth_write(false);
        ctx.set_blend(true);
        ctx.set_blend_func(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha);
        ctx.set_cull(CullMode::None);
        ctx.clear_scissor();

        text2d_.begin(&ctx, viewport_w_, viewport_h_, default_font_);
    }

    void Canvas2DRenderer::end() {
        flush_();
        text2d_.end();
        if (ctx_ != nullptr) {
            ctx_->clear_scissor();
        }
        clip_stack_.clear();
        ctx_ = nullptr;
    }

    bool Canvas2DRenderer::execute(const DrawList2D& list, DrawResourceResolver2D& resources) {
        if (ctx_ == nullptr) {
            tc::Log::error("[Canvas2DRenderer] execute requires an active Canvas frame");
            return false;
        }

        std::vector<termin::Affine2f> transforms{termin::Affine2f::identity()};
        std::vector<float> opacities{1.0f};
        std::vector<ClipMesh2D> clips;
        std::vector<NativeClip2D> native_clips{{}};

        const auto emit = [this, &clips](std::span<const DrawTriangle2D> triangles,
                                         termin::LinearColor color,
                                         TextureHandle texture = {},
                                         CanvasTextureSampling sampling = CanvasTextureSampling::Linear) {
            const auto vertices = flatten_and_clip(triangles, clips);
            append_mesh_(vertices, color, texture, sampling);
        };

        for (const auto& command : list.commands()) {
            const bool ok = std::visit(
                [&](const auto& value) -> bool {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, PushTransform2D>) {
                        transforms.push_back(transforms.back() * value.transform);
                        return true;
                    } else if constexpr (std::is_same_v<T, PopTransform2D>) {
                        if (transforms.size() <= 1)
                            return false;
                        transforms.pop_back();
                        return true;
                    } else if constexpr (std::is_same_v<T, PushOpacity2D>) {
                        opacities.push_back(opacities.back() * value.opacity);
                        return true;
                    } else if constexpr (std::is_same_v<T, PopOpacity2D>) {
                        if (opacities.size() <= 1)
                            return false;
                        opacities.pop_back();
                        return true;
                    } else if constexpr (std::is_same_v<T, PushClip2D>) {
                        ClipMesh2D mesh;
                        if (!tessellate_path(value.path, transforms.back(), value.rule, mesh)) {
                            return false;
                        }
                        clips.push_back(std::move(mesh));
                        native_clips.push_back(retained_clip(value.path, transforms.back(), native_clips.back()));
                        return true;
                    } else if constexpr (std::is_same_v<T, PopClip2D>) {
                        if (clips.empty() || native_clips.size() <= 1)
                            return false;
                        clips.pop_back();
                        native_clips.pop_back();
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawRect2D>) {
                        ClipMesh2D triangles;
                        if (!fill_contour(rect_contour(value.rect), transforms.back(), triangles)) {
                            return false;
                        }
                        emit(triangles, with_opacity(value.paint.color, opacities.back()));
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawRoundedRect2D>) {
                        const auto contour = rounded_rect_contour(value.rect, value.radius);
                        ClipMesh2D triangles;
                        if (!fill_contour(contour, transforms.back(), triangles)) {
                            return false;
                        }
                        emit(triangles, with_opacity(value.paint.color, opacities.back()));
                        if (value.stroke) {
                            const auto stroke = stroke_contour(contour, true, value.stroke->width, transforms.back());
                            emit(stroke, with_opacity(value.stroke->color, opacities.back()));
                        }
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawEllipse2D>) {
                        const auto contour = ellipse_contour(value.bounds);
                        ClipMesh2D triangles;
                        if (!fill_contour(contour, transforms.back(), triangles)) {
                            return false;
                        }
                        emit(triangles, with_opacity(value.paint.color, opacities.back()));
                        if (value.stroke) {
                            const auto stroke = stroke_contour(contour, true, value.stroke->width, transforms.back());
                            emit(stroke, with_opacity(value.stroke->color, opacities.back()));
                        }
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawPath2D>) {
                        if (value.fill) {
                            ClipMesh2D triangles;
                            if (!tessellate_path(value.path, transforms.back(), value.fill->rule, triangles)) {
                                return false;
                            }
                            emit(triangles, with_opacity(value.fill->color, opacities.back()));
                        }
                        if (value.stroke) {
                            const auto flattened = value.path.flatten(0.2f);
                            for (const auto& contour : flattened.contours) {
                                const auto stroke = stroke_contour(
                                    contour.points, contour.closed, value.stroke->width, transforms.back());
                                emit(stroke, with_opacity(value.stroke->color, opacities.back()));
                            }
                        }
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawPolyline2D>) {
                        const auto triangles =
                            stroke_contour(value.points, value.closed, value.stroke.width, transforms.back());
                        emit(triangles, with_opacity(value.stroke.color, opacities.back()));
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawImage2D>) {
                        const auto& transform = transforms.back();
                        const float x0 = value.rect.x;
                        const float y0 = value.rect.y;
                        const float x1 = value.rect.x + value.rect.width;
                        const float y1 = value.rect.y + value.rect.height;
                        const float u0 = value.uv.x;
                        const float v0 = value.uv.y;
                        const float u1 = value.uv.x + value.uv.width;
                        const float v1 = value.uv.y + value.uv.height;
                        const DrawVertex2D a{transform.transform_point({x0, y0}), {u0, v0}};
                        const DrawVertex2D b{transform.transform_point({x0, y1}), {u0, v1}};
                        const DrawVertex2D c{transform.transform_point({x1, y1}), {u1, v1}};
                        const DrawVertex2D d{transform.transform_point({x1, y0}), {u1, v0}};
                        const ClipMesh2D triangles{{{a, b, c}}, {{a, c, d}}};
                        emit(triangles,
                             with_opacity(value.tint, opacities.back()),
                             value.texture,
                             sampling_from(value.sampling));
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawCustomBatch2D>) {
                        ClipMesh2D triangles;
                        triangles.reserve(value.vertices.size() / 3);
                        for (std::size_t i = 0; i < value.vertices.size(); i += 3) {
                            DrawTriangle2D triangle{};
                            for (int vertex = 0; vertex < 3; ++vertex) {
                                triangle[vertex] = value.vertices[i + vertex];
                                triangle[vertex].position =
                                    transforms.back().transform_point(triangle[vertex].position);
                            }
                            triangles.push_back(triangle);
                        }
                        emit(triangles,
                             with_opacity(value.color, opacities.back()),
                             value.texture,
                             sampling_from(value.sampling));
                        return true;
                    } else if constexpr (std::is_same_v<T, DrawRetainedBatch2D>) {
                        if (!value.batch)
                            return false;
                        flush_();
                        const auto& clip = native_clips.back();
                        RetainedDrawState2D state{
                            .transform = transforms.back(),
                            .opacity = opacities.back(),
                            .clip_rect = clip.rect,
                            .has_clip_rect = clip.has_rect,
                            .unsupported_clip = clip.unsupported,
                            .viewport_x = viewport_x_,
                            .viewport_y = viewport_y_,
                            .viewport_width = viewport_w_,
                            .viewport_height = viewport_h_,
                        };
                        const bool drawn = value.batch->draw(*ctx_, state);
                        ctx_->set_viewport(viewport_x_, viewport_y_, viewport_w_, viewport_h_);
                        ctx_->set_scissor(viewport_x_, viewport_y_, viewport_w_, viewport_h_);
                        return drawn;
                    } else if constexpr (std::is_same_v<T, DrawText2D>) {
                        FontAtlas* font = resources.resolve_font(value.font);
                        if (font == nullptr) {
                            tc::Log::error("[Canvas2DRenderer] FontHandle %u did not resolve", value.font.id);
                            return false;
                        }
                        font->ensure_glyphs(value.text, value.size_px, ctx_);
                        const auto measured = font->measure_text(value.text, value.size_px);
                        float start_x = value.origin.x;
                        float start_y = value.origin.y;
                        if (value.anchor == TextAnchor2D::Center) {
                            start_x -= measured.width * 0.5f;
                            start_y -= value.size_px * 0.5f;
                        } else if (value.anchor == TextAnchor2D::Right) {
                            start_x -= measured.width;
                        }
                        const bool sdf = font->is_sdf_size(value.size_px);
                        const float spread = sdf ? static_cast<float>(font->sdf_spread()) * value.size_px /
                                                       static_cast<float>(font->sdf_reference_px())
                                                 : 0.0f;
                        ClipMesh2D triangles;
                        float cursor_x = start_x;
                        std::size_t byte_index = 0;
                        while (byte_index < value.text.size()) {
                            const std::uint32_t codepoint = internal::utf8_decode(value.text, byte_index);
                            const auto glyph = font->get_glyph(codepoint, value.size_px);
                            if (!glyph)
                                continue;
                            const float x0 = cursor_x;
                            const float x1 = x0 + glyph->width_px;
                            const float y0 = start_y - spread;
                            const float y1 = y0 + glyph->height_px;
                            const auto& transform = transforms.back();
                            const DrawVertex2D a{transform.transform_point({x0, y0}), {glyph->u0, glyph->v0}};
                            const DrawVertex2D b{transform.transform_point({x0, y1}), {glyph->u0, glyph->v1}};
                            const DrawVertex2D c{transform.transform_point({x1, y1}), {glyph->u1, glyph->v1}};
                            const DrawVertex2D d{transform.transform_point({x1, y0}), {glyph->u1, glyph->v0}};
                            triangles.push_back({{a, b, c}});
                            triangles.push_back({{a, c, d}});
                            cursor_x += glyph->advance_px;
                        }
                        const auto clipped = flatten_and_clip(triangles, clips);
                        if (clipped.empty())
                            return true;
                        flush_();
                        std::vector<Text2DVertex> text_vertices;
                        text_vertices.reserve(clipped.size());
                        for (const auto& vertex : clipped) {
                            text_vertices.push_back({{vertex.position.x - static_cast<float>(viewport_x_),
                                                      vertex.position.y - static_cast<float>(viewport_y_)},
                                                     vertex.uv});
                        }
                        text2d_.draw_mesh_linear(
                            text_vertices, with_opacity(value.color, opacities.back()), value.size_px, font);
                        return true;
                    }
                    return false;
                },
                command);
            if (!ok) {
                tc::Log::error("[Canvas2DRenderer] failed to execute DrawList2D command");
                return false;
            }
        }
        // Flush once at the list boundary so failures in the final batch are
        // observable to the caller.  Intermediate commands still retain the
        // existing batching behaviour and only flush on a state change.
        return flush_() && !batch_failed_;
    }

    void Canvas2DRenderer::begin_clip(float x, float y, float w, float h) {
        if (ctx_ == nullptr)
            return;
        flush_();

        ClipRect r;
        r.x = static_cast<int>(std::floor(x));
        r.y = static_cast<int>(std::floor(y));
        r.w = static_cast<int>(std::ceil(w));
        r.h = static_cast<int>(std::ceil(h));

        if (!clip_stack_.empty()) {
            const ClipRect& p = clip_stack_.back();
            const int x0 = std::max(r.x, p.x);
            const int y0 = std::max(r.y, p.y);
            const int x1 = std::min(r.x + r.w, p.x + p.w);
            const int y1 = std::min(r.y + r.h, p.y + p.h);
            r.x = x0;
            r.y = y0;
            r.w = std::max(0, x1 - x0);
            r.h = std::max(0, y1 - y0);
        }

        const int vx0 = std::max(0, r.x);
        const int vy0 = std::max(0, r.y);
        const int vx1 = std::min(viewport_w_, r.x + r.w);
        const int vy1 = std::min(viewport_h_, r.y + r.h);
        r.x = vx0;
        r.y = vy0;
        r.w = std::max(0, vx1 - vx0);
        r.h = std::max(0, vy1 - vy0);
        if (r.w == 0)
            r.x = std::min(std::max(0, r.x), viewport_w_);
        if (r.h == 0)
            r.y = std::min(std::max(0, r.y), viewport_h_);

        clip_stack_.push_back(r);
        ctx_->set_scissor(r.x, r.y, r.w, r.h);
    }

    void Canvas2DRenderer::end_clip() {
        if (ctx_ == nullptr)
            return;
        flush_();

        if (!clip_stack_.empty()) {
            clip_stack_.pop_back();
        }

        if (clip_stack_.empty()) {
            ctx_->clear_scissor();
        } else {
            const ClipRect& r = clip_stack_.back();
            ctx_->set_scissor(r.x, r.y, r.w, r.h);
        }
    }

    void Canvas2DRenderer::draw_rect(float x, float y, float w, float h, CanvasSrgbColor color, float radius) {
        if (ctx_ == nullptr || w <= 0.0f || h <= 0.0f)
            return;
        const termin::LinearColor linear = termin::srgb_to_linear(color);
        radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
        if (radius <= 0.0f) {
            append_solid_quad_(termin::Rect2f{x, y, w, h}.bounds(), linear);
            return;
        }

        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kCornerSegments = 6;
        std::vector<CanvasVec2> perimeter;
        perimeter.reserve(4 * (kCornerSegments + 1));
        const auto append_corner = [&perimeter, radius, kPi, kCornerSegments](float cx, float cy, float start_angle) {
            for (int segment = 0; segment <= kCornerSegments; ++segment) {
                const float angle =
                    start_angle + (kPi * 0.5f) * static_cast<float>(segment) / static_cast<float>(kCornerSegments);
                perimeter.push_back(CanvasVec2{
                    cx + std::cos(angle) * radius,
                    cy + std::sin(angle) * radius,
                });
            }
        };
        append_corner(x + radius, y + radius, kPi);
        append_corner(x + w - radius, y + radius, kPi * 1.5f);
        append_corner(x + w - radius, y + h - radius, 0.0f);
        append_corner(x + radius, y + h - radius, kPi * 0.5f);

        const CanvasVec2 center{x + w * 0.5f, y + h * 0.5f};
        for (size_t index = 0; index < perimeter.size(); ++index) {
            append_solid_triangle_(center, perimeter[index], perimeter[(index + 1) % perimeter.size()], linear);
        }
    }

    void Canvas2DRenderer::draw_circle(float cx, float cy, float radius, CanvasSrgbColor color, int segments) {
        if (ctx_ == nullptr || radius <= 0.0f)
            return;
        const termin::LinearColor linear = termin::srgb_to_linear(color);
        segments = std::clamp(segments, 8, 96);

        if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, linear)) {
            flush_();
            batch_mode_ = BatchMode::Solid;
            batch_color_ = linear;
            batch_texture_ = TextureHandle{};
        }

        constexpr float kTau = 6.2831853071795864769f;
        for (int i = 0; i < segments; ++i) {
            const float a0 = kTau * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = kTau * static_cast<float>(i + 1) / static_cast<float>(segments);
            const float x0 = cx + std::cos(a0) * radius;
            const float y0 = cy + std::sin(a0) * radius;
            const float x1 = cx + std::cos(a1) * radius;
            const float y1 = cy + std::sin(a1) * radius;
            const float tri[] = {
                cx,   cy,   0.0f, 0.5f, 0.5f, 0.0f, 0.0f, x0,   y0,   0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, x1,   y1,   0.0f, 1.0f, 1.0f, 0.0f, 0.0f,
            };
            batch_vertices_.insert(batch_vertices_.end(), std::begin(tri), std::end(tri));
        }
    }

    void Canvas2DRenderer::draw_circle_outline(
        float cx, float cy, float radius, CanvasSrgbColor color, float thickness, int segments) {
        constexpr float kTau = 6.2831853071795864769f;
        draw_arc(CanvasArc{{cx, cy}, radius, 0.0f, kTau, color, thickness, segments});
    }

    void Canvas2DRenderer::draw_arc(const CanvasArc& arc) {
        if (ctx_ == nullptr || arc.radius <= 0.0f || arc.thickness <= 0.0f || !std::isfinite(arc.start_radians) ||
            !std::isfinite(arc.end_radians)) {
            return;
        }
        constexpr float kTau = 6.2831853071795864769f;
        const float sweep = arc.end_radians - arc.start_radians;
        if (std::fabs(sweep) <= 0.0001f)
            return;
        int segments = arc.segments;
        if (segments <= 0) {
            segments = static_cast<int>(std::ceil(24.0f * std::fabs(sweep) / kTau));
        }
        segments = std::clamp(segments, 2, 192);
        std::vector<CanvasVec2> points;
        points.reserve(static_cast<size_t>(segments) + 1);
        for (int segment = 0; segment <= segments; ++segment) {
            const float t = static_cast<float>(segment) / static_cast<float>(segments);
            const float angle = arc.start_radians + sweep * t;
            points.push_back(CanvasVec2{
                arc.center.x + std::cos(angle) * arc.radius,
                arc.center.y + std::sin(angle) * arc.radius,
            });
        }
        draw_polyline(points, arc.color, arc.thickness);
    }

    void Canvas2DRenderer::draw_rect_outline(float x, float y, float w, float h, CanvasSrgbColor color, float thickness) {
        if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f)
            return;
        const float t = std::min(thickness, std::min(w, h));
        draw_rect(x, y, w, t, color);
        draw_rect(x, y + h - t, w, t, color);
        draw_rect(x, y, t, h, color);
        draw_rect(x + w - t, y, t, h, color);
    }

    void Canvas2DRenderer::draw_rounded_rect_outline(const CanvasRoundedRectOutline& outline) {
        const float x = outline.x;
        const float y = outline.y;
        const float w = outline.width;
        const float h = outline.height;
        const CanvasSrgbColor color = outline.color;
        const float thickness = outline.thickness;
        float radius = outline.radius;
        if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f)
            return;
        radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
        if (radius <= 0.0f) {
            draw_rect_outline(x, y, w, h, color, thickness);
            return;
        }
        constexpr float kPi = 3.14159265358979323846f;
        const int corner_segments = std::clamp(outline.corner_segments, 2, 48);
        draw_line(x + radius, y, x + w - radius, y, color, thickness);
        draw_line(x + w, y + radius, x + w, y + h - radius, color, thickness);
        draw_line(x + w - radius, y + h, x + radius, y + h, color, thickness);
        draw_line(x, y + h - radius, x, y + radius, color, thickness);
        draw_arc(CanvasArc{{x + radius, y + radius}, radius, kPi, kPi * 1.5f, color, thickness, corner_segments});
        draw_arc(
            CanvasArc{{x + w - radius, y + radius}, radius, kPi * 1.5f, kPi * 2.0f, color, thickness, corner_segments});
        draw_arc(
            CanvasArc{{x + w - radius, y + h - radius}, radius, 0.0f, kPi * 0.5f, color, thickness, corner_segments});
        draw_arc(CanvasArc{{x + radius, y + h - radius}, radius, kPi * 0.5f, kPi, color, thickness, corner_segments});
    }

    void Canvas2DRenderer::draw_line(float x0, float y0, float x1, float y1, CanvasSrgbColor color, float thickness) {
        if (ctx_ == nullptr || thickness <= 0.0f)
            return;

        const termin::LinearColor linear = termin::srgb_to_linear(color);
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f)
            return;

        const float half = thickness * 0.5f;
        const float nx = -dy / len * half;
        const float ny = dx / len * half;

        if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, linear)) {
            flush_();
            batch_mode_ = BatchMode::Solid;
            batch_color_ = linear;
            batch_texture_ = TextureHandle{};
        }

        const float ax = x0 + nx;
        const float ay = y0 + ny;
        const float bx = x0 - nx;
        const float by = y0 - ny;
        const float cx = x1 - nx;
        const float cy = y1 - ny;
        const float dxp = x1 + nx;
        const float dyp = y1 + ny;

        const float quad[] = {
            ax, ay, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, bx,  by,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            cx, cy, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, ax,  ay,  0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            cx, cy, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, dxp, dyp, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        };
        batch_vertices_.insert(batch_vertices_.end(), std::begin(quad), std::end(quad));
    }

    void Canvas2DRenderer::draw_polyline(std::span<const CanvasVec2> points, CanvasSrgbColor color, float thickness) {
        if (points.size() < 2)
            return;
        for (size_t i = 1; i < points.size(); ++i) {
            draw_line(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, color, thickness);
        }
    }

    void Canvas2DRenderer::draw_texture(TextureHandle texture,
                                        float x,
                                        float y,
                                        float w,
                                        float h,
                                        CanvasSrgbColor tint,
                                        bool flip_v,
                                        CanvasTextureSampling sampling) {
        if (ctx_ == nullptr || !texture || w <= 0.0f || h <= 0.0f)
            return;
        const float v0 = flip_v ? 1.0f : 0.0f;
        const float v1 = flip_v ? 0.0f : 1.0f;
        append_textured_quad_(
            termin::Rect2f{x, y, w, h}.bounds(),
            termin::Bounds2f{0.0f, v0, 1.0f, v1},
            termin::srgb_to_linear(tint),
            texture,
            sampling);
    }

    void Canvas2DRenderer::draw_text(std::string_view text,
                                     float x,
                                     float y,
                                     float size_px,
                                     CanvasSrgbColor color,
                                     FontAtlas* font,
                                     Text2DRenderer::Anchor anchor) {
        if (ctx_ == nullptr || text.empty())
            return;
        FontAtlas* active_font = font ? font : default_font_;
        if (active_font == nullptr)
            return;

        flush_();
        text2d_.draw(text,
                     Text2DRenderer::DrawOptions{x - static_cast<float>(viewport_x_),
                                                 y - static_cast<float>(viewport_y_),
                                                 color,
                                                 size_px,
                                                 anchor});
    }

    FontAtlas::Size2f Canvas2DRenderer::measure_text(std::string_view text, float size_px, FontAtlas* font) const {
        FontAtlas* active_font = font ? font : default_font_;
        if (active_font == nullptr || text.empty())
            return {};
        active_font->ensure_glyphs(text, size_px);
        return active_font->measure_text(text, size_px);
    }

    void Canvas2DRenderer::release_gpu() {
        flush_();
        text2d_.release_gpu();
        if (samplers_on_ != nullptr) {
            if (linear_sampler_)
                samplers_on_->destroy(linear_sampler_);
            if (nearest_sampler_)
                samplers_on_->destroy(nearest_sampler_);
        }
        linear_sampler_ = SamplerHandle{};
        nearest_sampler_ = SamplerHandle{};
        samplers_on_ = nullptr;
        solid_vs_ = ShaderHandle{};
        solid_fs_ = ShaderHandle{};
        texture_vs_ = ShaderHandle{};
        texture_fs_ = ShaderHandle{};
        compiled_on_ = nullptr;
    }

    void Canvas2DRenderer::ensure_samplers_(IRenderDevice& device) {
        if (samplers_on_ == &device && linear_sampler_ && nearest_sampler_)
            return;
        if (samplers_on_ != nullptr) {
            if (linear_sampler_)
                samplers_on_->destroy(linear_sampler_);
            if (nearest_sampler_)
                samplers_on_->destroy(nearest_sampler_);
        }
        linear_sampler_ = SamplerHandle{};
        nearest_sampler_ = SamplerHandle{};
        samplers_on_ = &device;

        SamplerDesc linear_desc{};
        linear_desc.address_u = AddressMode::ClampToEdge;
        linear_desc.address_v = AddressMode::ClampToEdge;
        linear_desc.address_w = AddressMode::ClampToEdge;
        linear_sampler_ = device.create_sampler(linear_desc);

        SamplerDesc nearest_desc = linear_desc;
        nearest_desc.min_filter = FilterMode::Nearest;
        nearest_desc.mag_filter = FilterMode::Nearest;
        nearest_desc.mip_filter = FilterMode::Nearest;
        nearest_sampler_ = device.create_sampler(nearest_desc);
        if (!linear_sampler_ || !nearest_sampler_) {
            tc::Log::error("[Canvas2DRenderer] failed to create texture samplers");
        }
    }

    void Canvas2DRenderer::ensure_shaders_(IRenderDevice& device) {
        if (compiled_on_ == &device && solid_vs_.id != 0 && solid_fs_.id != 0 && texture_vs_.id != 0 &&
            texture_fs_.id != 0) {
            return;
        }

        solid_vs_ = ShaderHandle{};
        solid_fs_ = ShaderHandle{};
        texture_vs_ = ShaderHandle{};
        texture_fs_ = ShaderHandle{};

        if (tc_shader* raw = tc_shader_get(solid_shader_handle())) {
            if (!termin::tc_shader_ensure_tgfx2(raw, &device, &solid_vs_, &solid_fs_)) {
                tc::Log::error("[Canvas2DRenderer] failed to create solid shader");
            }
        }
        if (tc_shader* raw = tc_shader_get(texture_shader_handle())) {
            if (!termin::tc_shader_ensure_tgfx2(raw, &device, &texture_vs_, &texture_fs_)) {
                tc::Log::error("[Canvas2DRenderer] failed to create texture shader");
            }
        }

        if (solid_vs_.id == 0 || solid_fs_.id == 0) {
            tc::Log::error("[Canvas2DRenderer] solid shader is unavailable");
        }

        if (texture_vs_.id == 0 || texture_fs_.id == 0) {
            tc::Log::error("[Canvas2DRenderer] texture shader is unavailable");
        }

        compiled_on_ = &device;
    }

    void Canvas2DRenderer::build_projection_() {
        build_ortho_pixel_to_ndc(static_cast<float>(viewport_x_),
                                 static_cast<float>(viewport_y_),
                                 static_cast<float>(viewport_w_),
                                 static_cast<float>(viewport_h_),
                                 projection_);
    }

    bool Canvas2DRenderer::flush_() {
        if (ctx_ == nullptr || batch_vertices_.empty())
            return true;

        bool bound = false;
        if (batch_mode_ == BatchMode::Solid) {
            bound = bind_solid_(batch_color_);
        } else if (batch_mode_ == BatchMode::Texture) {
            bound = bind_texture_(batch_color_, batch_texture_, batch_texture_sampling_);
        } else {
            batch_vertices_.clear();
            return true;
        }
        if (!bound) {
            batch_vertices_.clear();
            batch_failed_ = true;
            return false;
        }

        const uint32_t vertex_count = static_cast<uint32_t>(batch_vertices_.size() / 7);
        ctx_->draw_immediate_triangles(batch_vertices_.data(), vertex_count);
        batch_vertices_.clear();
        return true;
    }

    bool Canvas2DRenderer::bind_solid_(termin::LinearColor color) {
        if (solid_vs_.id == 0 || solid_fs_.id == 0) {
            tc::Log::error("[Canvas2DRenderer] solid shader is unavailable; skipping batch");
            return false;
        }

        CanvasPushData push;
        std::memcpy(push.projection, projection_, sizeof(projection_));
        push.color[0] = color.r;
        push.color[1] = color.g;
        push.color[2] = color.b;
        push.color[3] = color.a;

        ctx_->bind_shader(solid_vs_, solid_fs_);
        tc_shader* raw = tc_shader_get(solid_shader_handle());
        ctx_->use_shader_resource_layout(raw);
        ctx_->bind_uniform_data("canvas_draw", &push, static_cast<uint32_t>(sizeof(push)));
        return true;
    }

    bool Canvas2DRenderer::bind_texture_(termin::LinearColor tint,
                                         TextureHandle texture,
                                         CanvasTextureSampling sampling) {
        if (texture_vs_.id == 0 || texture_fs_.id == 0) {
            tc::Log::error("[Canvas2DRenderer] texture shader is unavailable; skipping batch");
            return false;
        }
        if (texture.id == 0) {
            tc::Log::error("[Canvas2DRenderer] texture batch has no texture; skipping batch");
            return false;
        }

        CanvasPushData push;
        std::memcpy(push.projection, projection_, sizeof(projection_));
        push.color[0] = tint.r;
        push.color[1] = tint.g;
        push.color[2] = tint.b;
        push.color[3] = tint.a;

        ctx_->bind_shader(texture_vs_, texture_fs_);
        tc_shader* raw = tc_shader_get(texture_shader_handle());
        ctx_->use_shader_resource_layout(raw);
        ctx_->bind_uniform_data("canvas_draw", &push, static_cast<uint32_t>(sizeof(push)));
        const SamplerHandle sampler = sampling == CanvasTextureSampling::Nearest ? nearest_sampler_ : linear_sampler_;
        if (!sampler) {
            tc::Log::error("[Canvas2DRenderer] texture sampler is unavailable; skipping batch");
            return false;
        }
        ctx_->bind_texture("u_texture", texture, sampler);
        return true;
    }

    void Canvas2DRenderer::push_quad_(termin::Bounds2f bounds, termin::Bounds2f uv) {
        const float quad[] = {
            bounds.x0, bounds.y0, 0.0f, uv.x0, uv.y0, 0.0f, 0.0f, bounds.x0, bounds.y1, 0.0f, uv.x0, uv.y1, 0.0f, 0.0f,
            bounds.x1, bounds.y1, 0.0f, uv.x1, uv.y1, 0.0f, 0.0f, bounds.x0, bounds.y0, 0.0f, uv.x0, uv.y0, 0.0f, 0.0f,
            bounds.x1, bounds.y1, 0.0f, uv.x1, uv.y1, 0.0f, 0.0f, bounds.x1, bounds.y0, 0.0f, uv.x1, uv.y0, 0.0f, 0.0f,
        };
        batch_vertices_.insert(batch_vertices_.end(), std::begin(quad), std::end(quad));
    }

    void Canvas2DRenderer::append_solid_quad_(termin::Bounds2f bounds, termin::LinearColor color) {
        if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
            flush_();
            batch_mode_ = BatchMode::Solid;
            batch_color_ = color;
            batch_texture_ = TextureHandle{};
        }
        push_quad_(bounds, termin::Bounds2f{0.0f, 0.0f, 1.0f, 1.0f});
    }

    void Canvas2DRenderer::append_solid_triangle_(
        CanvasVec2 p0, CanvasVec2 p1, CanvasVec2 p2, termin::LinearColor color) {
        if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
            flush_();
            batch_mode_ = BatchMode::Solid;
            batch_color_ = color;
            batch_texture_ = TextureHandle{};
        }
        const float triangle[] = {
            p0.x, p0.y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, p1.x, p1.y, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, p2.x, p2.y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        };
        batch_vertices_.insert(batch_vertices_.end(), std::begin(triangle), std::end(triangle));
    }

    void Canvas2DRenderer::append_textured_quad_(termin::Bounds2f bounds,
                                                 termin::Bounds2f uv,
                                                 termin::LinearColor tint,
                                                 TextureHandle texture,
                                                 CanvasTextureSampling sampling) {
        if (batch_mode_ != BatchMode::Texture || !same_color(batch_color_, tint) || batch_texture_.id != texture.id ||
            batch_texture_sampling_ != sampling) {
            flush_();
            batch_mode_ = BatchMode::Texture;
            batch_color_ = tint;
            batch_texture_ = texture;
            batch_texture_sampling_ = sampling;
        }
        push_quad_(bounds, uv);
    }

    void Canvas2DRenderer::append_mesh_(std::span<const DrawVertex2D> vertices,
                                        termin::LinearColor color,
                                        TextureHandle texture,
                                        CanvasTextureSampling sampling) {
        if (vertices.empty())
            return;
        if (texture) {
            if (batch_mode_ != BatchMode::Texture || !same_color(batch_color_, color) || batch_texture_ != texture ||
                batch_texture_sampling_ != sampling) {
                flush_();
                batch_mode_ = BatchMode::Texture;
                batch_color_ = color;
                batch_texture_ = texture;
                batch_texture_sampling_ = sampling;
            }
        } else if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
            flush_();
            batch_mode_ = BatchMode::Solid;
            batch_color_ = color;
            batch_texture_ = {};
        }
        for (const auto& vertex : vertices) {
            batch_vertices_.insert(batch_vertices_.end(),
                                   {vertex.position.x, vertex.position.y, 0.0f, vertex.uv.x, vertex.uv.y, 0.0f, 0.0f});
        }
    }

} // namespace tgfx
