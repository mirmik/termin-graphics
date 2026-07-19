// canvas2d_renderer.cpp - Reusable immediate 2D drawing facade for tgfx2.

#include "tgfx2/canvas2d_renderer.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

#include <tcbase/tc_log.hpp>

namespace tgfx {

namespace {

struct CanvasPushData {
    float projection[16];
    float color[4];
};
static_assert(sizeof(CanvasPushData) == 80,
              "CanvasPushData layout drift - shader and C++ disagree");

constexpr const char* CANVAS2D_SOLID_SHADER_UUID = "termin-engine-canvas2d-solid";
constexpr const char* CANVAS2D_TEXTURE_SHADER_UUID = "termin-engine-canvas2d-texture";

tc_shader_handle solid_shader_handle() {
    static tc_shader_handle handle = tc_shader_handle_invalid();
    if (tc_shader_handle_is_invalid(handle)) {
        handle = register_builtin_shader_from_catalog(CANVAS2D_SOLID_SHADER_UUID);
    }
    return handle;
}

tc_shader_handle texture_shader_handle() {
    static tc_shader_handle handle = tc_shader_handle_invalid();
    if (tc_shader_handle_is_invalid(handle)) {
        handle = register_builtin_shader_from_catalog(CANVAS2D_TEXTURE_SHADER_UUID);
    }
    return handle;
}

void build_ortho_pixel_to_ndc(
    float x,
    float y,
    float w,
    float h,
    float out[16])
{
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

bool same_color(CanvasColor a, CanvasColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

}  // namespace

Canvas2DRenderer::Canvas2DRenderer(FontAtlas* default_font)
    : default_font_(default_font), text2d_(default_font) {}

Canvas2DRenderer::~Canvas2DRenderer() {
    release_gpu();
}

void Canvas2DRenderer::begin(RenderContext2& ctx, int width, int height) {
    begin(ctx, 0, 0, width, height);
}

void Canvas2DRenderer::begin(RenderContext2& ctx,
                             int x, int y, int width, int height) {
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

void Canvas2DRenderer::begin_clip(float x, float y, float w, float h) {
    if (ctx_ == nullptr) return;
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
    if (r.w == 0) r.x = std::min(std::max(0, r.x), viewport_w_);
    if (r.h == 0) r.y = std::min(std::max(0, r.y), viewport_h_);

    clip_stack_.push_back(r);
    ctx_->set_scissor(r.x, r.y, r.w, r.h);
}

void Canvas2DRenderer::end_clip() {
    if (ctx_ == nullptr) return;
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

void Canvas2DRenderer::draw_rect(float x, float y, float w, float h,
                                 CanvasColor color, float radius) {
    if (ctx_ == nullptr || w <= 0.0f || h <= 0.0f) return;
    radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
    if (radius <= 0.0f) {
        append_solid_quad_(termin::Rect2f{x, y, w, h}.bounds(), color);
        return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kCornerSegments = 6;
    std::vector<CanvasVec2> perimeter;
    perimeter.reserve(4 * (kCornerSegments + 1));
    const auto append_corner = [&perimeter, radius, kPi, kCornerSegments](
        float cx,
        float cy,
        float start_angle
    ) {
        for (int segment = 0; segment <= kCornerSegments; ++segment) {
            const float angle = start_angle +
                (kPi * 0.5f) * static_cast<float>(segment) /
                    static_cast<float>(kCornerSegments);
            perimeter.push_back(CanvasVec2 {
                cx + std::cos(angle) * radius,
                cy + std::sin(angle) * radius,
            });
        }
    };
    append_corner(x + radius, y + radius, kPi);
    append_corner(x + w - radius, y + radius, kPi * 1.5f);
    append_corner(x + w - radius, y + h - radius, 0.0f);
    append_corner(x + radius, y + h - radius, kPi * 0.5f);

    const CanvasVec2 center {x + w * 0.5f, y + h * 0.5f};
    for (size_t index = 0; index < perimeter.size(); ++index) {
        append_solid_triangle_(
            center,
            perimeter[index],
            perimeter[(index + 1) % perimeter.size()],
            color
        );
    }
}

void Canvas2DRenderer::draw_circle(float cx, float cy, float radius,
                                   CanvasColor color, int segments) {
    if (ctx_ == nullptr || radius <= 0.0f) return;
    segments = std::clamp(segments, 8, 96);

    if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
        flush_();
        batch_mode_ = BatchMode::Solid;
        batch_color_ = color;
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
            cx, cy, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f,
            x0, y0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            x1, y1, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        };
        batch_vertices_.insert(batch_vertices_.end(), std::begin(tri), std::end(tri));
    }
}

void Canvas2DRenderer::draw_circle_outline(
    float cx,
    float cy,
    float radius,
    CanvasColor color,
    float thickness,
    int segments
) {
    constexpr float kTau = 6.2831853071795864769f;
    draw_arc(CanvasArc{{cx, cy}, radius, 0.0f, kTau, color, thickness, segments});
}

void Canvas2DRenderer::draw_arc(const CanvasArc& arc) {
    if (ctx_ == nullptr || arc.radius <= 0.0f || arc.thickness <= 0.0f ||
        !std::isfinite(arc.start_radians) || !std::isfinite(arc.end_radians)) {
        return;
    }
    constexpr float kTau = 6.2831853071795864769f;
    const float sweep = arc.end_radians - arc.start_radians;
    if (std::fabs(sweep) <= 0.0001f) return;
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
        points.push_back(CanvasVec2 {
            arc.center.x + std::cos(angle) * arc.radius,
            arc.center.y + std::sin(angle) * arc.radius,
        });
    }
    draw_polyline(points, arc.color, arc.thickness);
}

void Canvas2DRenderer::draw_rect_outline(float x, float y, float w, float h,
                                         CanvasColor color, float thickness) {
    if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f) return;
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
    const CanvasColor color = outline.color;
    const float thickness = outline.thickness;
    float radius = outline.radius;
    if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f) return;
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
    draw_arc(CanvasArc{{x + radius, y + radius}, radius, kPi, kPi * 1.5f,
                       color, thickness, corner_segments});
    draw_arc(CanvasArc{{x + w - radius, y + radius}, radius, kPi * 1.5f, kPi * 2.0f,
                       color, thickness, corner_segments});
    draw_arc(CanvasArc{{x + w - radius, y + h - radius}, radius, 0.0f, kPi * 0.5f,
                       color, thickness, corner_segments});
    draw_arc(CanvasArc{{x + radius, y + h - radius}, radius, kPi * 0.5f, kPi,
                       color, thickness, corner_segments});
}

void Canvas2DRenderer::draw_line(float x0, float y0, float x1, float y1,
                                 CanvasColor color, float thickness) {
    if (ctx_ == nullptr || thickness <= 0.0f) return;

    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    const float half = thickness * 0.5f;
    const float nx = -dy / len * half;
    const float ny =  dx / len * half;

    if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
        flush_();
        batch_mode_ = BatchMode::Solid;
        batch_color_ = color;
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
        ax, ay, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        bx, by, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        cx, cy, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        ax, ay, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        cx, cy, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        dxp, dyp, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };
    batch_vertices_.insert(batch_vertices_.end(), std::begin(quad), std::end(quad));
}

void Canvas2DRenderer::draw_polyline(std::span<const CanvasVec2> points,
                                     CanvasColor color, float thickness) {
    if (points.size() < 2) return;
    for (size_t i = 1; i < points.size(); ++i) {
        draw_line(points[i - 1].x, points[i - 1].y,
                  points[i].x, points[i].y,
                  color, thickness);
    }
}

void Canvas2DRenderer::draw_texture(TextureHandle texture,
                                    float x, float y, float w, float h,
                                    CanvasColor tint, bool flip_v,
                                    CanvasTextureSampling sampling) {
    if (ctx_ == nullptr || !texture || w <= 0.0f || h <= 0.0f) return;
    const float v0 = flip_v ? 1.0f : 0.0f;
    const float v1 = flip_v ? 0.0f : 1.0f;
    append_textured_quad_(
        termin::Rect2f{x, y, w, h}.bounds(),
        termin::Bounds2f{0.0f, v0, 1.0f, v1},
        tint,
        texture,
        sampling);
}

void Canvas2DRenderer::draw_text(std::string_view text,
                                 float x, float y,
                                 float size_px,
                                 CanvasColor color,
                                 FontAtlas* font,
                                 Text2DRenderer::Anchor anchor) {
    if (ctx_ == nullptr || text.empty()) return;
    FontAtlas* active_font = font ? font : default_font_;
    if (active_font == nullptr) return;

    flush_();
    text2d_.draw(text, Text2DRenderer::DrawOptions{
        x - static_cast<float>(viewport_x_),
        y - static_cast<float>(viewport_y_),
        termin::Color4{color.r, color.g, color.b, color.a},
        size_px,
        anchor
    });
}

FontAtlas::Size2f Canvas2DRenderer::measure_text(std::string_view text,
                                                 float size_px,
                                                 FontAtlas* font) const {
    FontAtlas* active_font = font ? font : default_font_;
    if (active_font == nullptr || text.empty()) return {};
    active_font->ensure_glyphs(text, size_px);
    return active_font->measure_text(text, size_px);
}

void Canvas2DRenderer::release_gpu() {
    flush_();
    text2d_.release_gpu();
    if (samplers_on_ != nullptr) {
        if (linear_sampler_) samplers_on_->destroy(linear_sampler_);
        if (nearest_sampler_) samplers_on_->destroy(nearest_sampler_);
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
    if (samplers_on_ == &device && linear_sampler_ && nearest_sampler_) return;
    if (samplers_on_ != nullptr) {
        if (linear_sampler_) samplers_on_->destroy(linear_sampler_);
        if (nearest_sampler_) samplers_on_->destroy(nearest_sampler_);
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
    if (compiled_on_ == &device
        && solid_vs_.id != 0 && solid_fs_.id != 0
        && texture_vs_.id != 0 && texture_fs_.id != 0) {
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

void Canvas2DRenderer::flush_() {
    if (ctx_ == nullptr || batch_vertices_.empty()) return;

    bool bound = false;
    if (batch_mode_ == BatchMode::Solid) {
        bound = bind_solid_(batch_color_);
    } else if (batch_mode_ == BatchMode::Texture) {
        bound = bind_texture_(batch_color_, batch_texture_, batch_texture_sampling_);
    } else {
        batch_vertices_.clear();
        return;
    }
    if (!bound) {
        batch_vertices_.clear();
        return;
    }

    const uint32_t vertex_count =
        static_cast<uint32_t>(batch_vertices_.size() / 7);
    ctx_->draw_immediate_triangles(batch_vertices_.data(), vertex_count);
    batch_vertices_.clear();
}

bool Canvas2DRenderer::bind_solid_(CanvasColor color) {
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

bool Canvas2DRenderer::bind_texture_(CanvasColor tint, TextureHandle texture,
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
    const SamplerHandle sampler = sampling == CanvasTextureSampling::Nearest
        ? nearest_sampler_
        : linear_sampler_;
    if (!sampler) {
        tc::Log::error("[Canvas2DRenderer] texture sampler is unavailable; skipping batch");
        return false;
    }
    ctx_->bind_texture("u_texture", texture, sampler);
    return true;
}

void Canvas2DRenderer::push_quad_(termin::Bounds2f bounds, termin::Bounds2f uv) {
    const float quad[] = {
        bounds.x0, bounds.y0, 0.0f, uv.x0, uv.y0, 0.0f, 0.0f,
        bounds.x0, bounds.y1, 0.0f, uv.x0, uv.y1, 0.0f, 0.0f,
        bounds.x1, bounds.y1, 0.0f, uv.x1, uv.y1, 0.0f, 0.0f,
        bounds.x0, bounds.y0, 0.0f, uv.x0, uv.y0, 0.0f, 0.0f,
        bounds.x1, bounds.y1, 0.0f, uv.x1, uv.y1, 0.0f, 0.0f,
        bounds.x1, bounds.y0, 0.0f, uv.x1, uv.y0, 0.0f, 0.0f,
    };
    batch_vertices_.insert(batch_vertices_.end(), std::begin(quad), std::end(quad));
}

void Canvas2DRenderer::append_solid_quad_(termin::Bounds2f bounds, CanvasColor color) {
    if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
        flush_();
        batch_mode_ = BatchMode::Solid;
        batch_color_ = color;
        batch_texture_ = TextureHandle{};
    }
    push_quad_(bounds, termin::Bounds2f{0.0f, 0.0f, 1.0f, 1.0f});
}

void Canvas2DRenderer::append_solid_triangle_(
    CanvasVec2 p0,
    CanvasVec2 p1,
    CanvasVec2 p2,
    CanvasColor color
) {
    if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
        flush_();
        batch_mode_ = BatchMode::Solid;
        batch_color_ = color;
        batch_texture_ = TextureHandle{};
    }
    const float triangle[] = {
        p0.x, p0.y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        p1.x, p1.y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        p2.x, p2.y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    };
    batch_vertices_.insert(batch_vertices_.end(), std::begin(triangle), std::end(triangle));
}

void Canvas2DRenderer::append_textured_quad_(
    termin::Bounds2f bounds,
    termin::Bounds2f uv,
    CanvasColor tint,
    TextureHandle texture,
    CanvasTextureSampling sampling
) {
    if (batch_mode_ != BatchMode::Texture
        || !same_color(batch_color_, tint)
        || batch_texture_.id != texture.id
        || batch_texture_sampling_ != sampling) {
        flush_();
        batch_mode_ = BatchMode::Texture;
        batch_color_ = tint;
        batch_texture_ = texture;
        batch_texture_sampling_ = sampling;
    }
    push_quad_(bounds, uv);
}

}  // namespace tgfx
