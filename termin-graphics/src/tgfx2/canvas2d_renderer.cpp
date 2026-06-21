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
    bool d3d11_clip_space,
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
    float rm[16]{};
    rm[0] = 2.0f / w;
    rm[3] = -1.0f - 2.0f * x / w;
    if (d3d11_clip_space) {
        // D3D viewport transform maps clip-space y=+1 to the top edge.
        // Vulkan and OpenGL-with-upper-left clip control map y=-1 there.
        rm[5] = -2.0f / h;
        rm[7] = 1.0f + 2.0f * y / h;
    } else {
        rm[5] = 2.0f / h;
        rm[7] = -1.0f - 2.0f * y / h;
    }
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

    ensure_shaders_(ctx.device());
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
                                 CanvasColor color, float /*radius*/) {
    if (ctx_ == nullptr || w <= 0.0f || h <= 0.0f) return;
    append_solid_quad_(x, y, x + w, y + h, color);
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

void Canvas2DRenderer::draw_rect_outline(float x, float y, float w, float h,
                                         CanvasColor color, float thickness) {
    if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f) return;
    const float t = std::min(thickness, std::min(w, h));
    draw_rect(x, y, w, t, color);
    draw_rect(x, y + h - t, w, t, color);
    draw_rect(x, y, t, h, color);
    draw_rect(x + w - t, y, t, h, color);
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
                                    CanvasColor tint, bool flip_v) {
    if (ctx_ == nullptr || !texture || w <= 0.0f || h <= 0.0f) return;
    const float v0 = flip_v ? 1.0f : 0.0f;
    const float v1 = flip_v ? 0.0f : 1.0f;
    append_textured_quad_(x, y, x + w, y + h,
                          0.0f, v0, 1.0f, v1,
                          tint, texture);
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
    text2d_.draw(text,
                 x - static_cast<float>(viewport_x_),
                 y - static_cast<float>(viewport_y_),
                 color.r, color.g, color.b, color.a,
                 size_px, anchor);
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
    solid_vs_ = ShaderHandle{};
    solid_fs_ = ShaderHandle{};
    texture_vs_ = ShaderHandle{};
    texture_fs_ = ShaderHandle{};
    compiled_on_ = nullptr;
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
                             ctx_ != nullptr &&
                                 ctx_->device().backend_type() == BackendType::D3D11,
                             projection_);
}

void Canvas2DRenderer::flush_() {
    if (ctx_ == nullptr || batch_vertices_.empty()) return;

    bool bound = false;
    if (batch_mode_ == BatchMode::Solid) {
        bound = bind_solid_(batch_color_);
    } else if (batch_mode_ == BatchMode::Texture) {
        bound = bind_texture_(batch_color_, batch_texture_);
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

bool Canvas2DRenderer::bind_texture_(CanvasColor tint, TextureHandle texture) {
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
    ctx_->bind_texture("u_texture", texture);
    return true;
}

void Canvas2DRenderer::push_quad_(float x0, float y0, float x1, float y1,
                                  float u0, float v0, float u1, float v1) {
    const float quad[] = {
        x0, y0, 0.0f, u0, v0, 0.0f, 0.0f,
        x0, y1, 0.0f, u0, v1, 0.0f, 0.0f,
        x1, y1, 0.0f, u1, v1, 0.0f, 0.0f,
        x0, y0, 0.0f, u0, v0, 0.0f, 0.0f,
        x1, y1, 0.0f, u1, v1, 0.0f, 0.0f,
        x1, y0, 0.0f, u1, v0, 0.0f, 0.0f,
    };
    batch_vertices_.insert(batch_vertices_.end(), std::begin(quad), std::end(quad));
}

void Canvas2DRenderer::append_solid_quad_(float x0, float y0,
                                          float x1, float y1,
                                          CanvasColor color) {
    if (batch_mode_ != BatchMode::Solid || !same_color(batch_color_, color)) {
        flush_();
        batch_mode_ = BatchMode::Solid;
        batch_color_ = color;
        batch_texture_ = TextureHandle{};
    }
    push_quad_(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f);
}

void Canvas2DRenderer::append_textured_quad_(float x0, float y0,
                                             float x1, float y1,
                                             float u0, float v0,
                                             float u1, float v1,
                                             CanvasColor tint,
                                             TextureHandle texture) {
    if (batch_mode_ != BatchMode::Texture
        || !same_color(batch_color_, tint)
        || batch_texture_.id != texture.id) {
        flush_();
        batch_mode_ = BatchMode::Texture;
        batch_color_ = tint;
        batch_texture_ = texture;
    }
    push_quad_(x0, y0, x1, y1, u0, v0, u1, v1);
}

}  // namespace tgfx
