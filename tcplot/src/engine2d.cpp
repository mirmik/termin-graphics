// engine2d.cpp - 2D plot engine. Port of engine2d.py.
//
// Architectural change vs Python: we draw every line segment of a
// series into ONE flat vertex buffer and issue a single
// draw_immediate_lines(). The Python path issued one draw per segment
// which made 400-point sines visibly lag during pan. All other
// coordinate / bounds / pick math stays 1:1.

#include "tcplot/engine2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/text2d_renderer.hpp>

#include "tcplot/axes.hpp"

namespace tcplot {

namespace {

// Pos + color shader used by rects, grid and series lines.
// Ortho pixel→NDC projection, y+ down.
constexpr const char* kVert = R"(#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec4 a_color;
uniform mat4 u_projection;
out vec4 v_color;
void main() {
    gl_Position = u_projection * vec4(a_pos.xy, 0.0, 1.0);
    v_color = a_color;
}
)";

constexpr const char* kFrag = R"(#version 330 core
in vec4 v_color;
out vec4 frag_color;
void main() { frag_color = v_color; }
)";

// Ortho pixel→NDC, y+down input → y+up NDC.
void build_ortho(float w, float h, float out[16]) {
    if (w <= 0.0f || h <= 0.0f) {
        std::memset(out, 0, 16 * sizeof(float));
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }
    // Row-major here (we upload with transpose=true). Mirrors what
    // Text2D does, to stay consistent across the engine.
    const float m[16] = {
        2.0f / w,  0.0f,      0.0f, -1.0f,
        0.0f,     -2.0f / h,  0.0f,  1.0f,
        0.0f,      0.0f,     -1.0f,  0.0f,
        0.0f,      0.0f,      0.0f,  1.0f,
    };
    std::memcpy(out, m, sizeof(m));
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PlotEngine2D::PlotEngine2D()
    : text2d_(std::make_unique<tgfx2::Text2DRenderer>()) {}

PlotEngine2D::~PlotEngine2D() {
    release_gpu_resources();
}

void PlotEngine2D::set_viewport(float x, float y, float width, float height) {
    vx_ = x;
    vy_ = y;
    vw_ = width;
    vh_ = height;
}

// ---------------------------------------------------------------------------
// Series
// ---------------------------------------------------------------------------

void PlotEngine2D::plot(std::vector<double> x, std::vector<double> y,
                         std::optional<Color4> color,
                         double thickness,
                         std::string label) {
    data.add_line(std::move(x), std::move(y), {}, color, thickness, std::move(label));
    if (!view_x_min_.has_value()) fit();
}

void PlotEngine2D::scatter(std::vector<double> x, std::vector<double> y,
                            std::optional<Color4> color,
                            double size,
                            std::string label) {
    data.add_scatter(std::move(x), std::move(y), {}, color, size, std::move(label));
    if (!view_x_min_.has_value()) fit();
}

void PlotEngine2D::clear() {
    data = PlotData{};
    view_x_min_.reset();
    view_x_max_.reset();
    view_y_min_.reset();
    view_y_max_.reset();
}

void PlotEngine2D::fit() {
    const auto bounds = data.data_bounds_2d();
    const double x0 = bounds[0], x1 = bounds[1];
    const double y0 = bounds[2], y1 = bounds[3];
    const double dx = (x1 > x0) ? (x1 - x0) : 1.0;
    const double dy = (y1 > y0) ? (y1 - y0) : 1.0;
    constexpr double pad = 0.05;
    view_x_min_ = x0 - dx * pad;
    view_x_max_ = x1 + dx * pad;
    view_y_min_ = y0 - dy * pad;
    view_y_max_ = y1 + dy * pad;
}

void PlotEngine2D::set_view(double x_min, double x_max, double y_min, double y_max) {
    view_x_min_ = x_min;
    view_x_max_ = x_max;
    view_y_min_ = y_min;
    view_y_max_ = y_max;
}

// ---------------------------------------------------------------------------
// Coord helpers
// ---------------------------------------------------------------------------

PlotEngine2D::Rect PlotEngine2D::plot_area_() const {
    Rect r;
    r.x = vx_ + margin_left;
    r.y = vy_ + margin_top;
    r.w = std::max(vw_ - margin_left - margin_right, 1.0f);
    r.h = std::max(vh_ - margin_top - margin_bottom, 1.0f);
    return r;
}

PlotEngine2D::ViewRange PlotEngine2D::view_range_() {
    if (!view_x_min_.has_value()) fit();
    return {*view_x_min_, *view_x_max_, *view_y_min_, *view_y_max_};
}

void PlotEngine2D::data_to_pixel_(double dx, double dy,
                                    float& out_x, float& out_y) {
    const Rect pa = plot_area_();
    const ViewRange v = view_range_();
    const double sx = (v.x_max != v.x_min)
        ? (dx - v.x_min) / (v.x_max - v.x_min) : 0.5;
    const double sy = (v.y_max != v.y_min)
        ? (dy - v.y_min) / (v.y_max - v.y_min) : 0.5;
    out_x = pa.x + (float)sx * pa.w;
    out_y = pa.y + (1.0f - (float)sy) * pa.h;  // Y flipped (data y+up → pixel y+down)
}

void PlotEngine2D::pixel_to_data_(float wx, float wy,
                                    double& out_x, double& out_y) {
    const Rect pa = plot_area_();
    const ViewRange v = view_range_();
    const double sx = (wx - pa.x) / pa.w;
    const double sy = 1.0 - (wy - pa.y) / pa.h;
    out_x = v.x_min + sx * (v.x_max - v.x_min);
    out_y = v.y_min + sy * (v.y_max - v.y_min);
}

// ---------------------------------------------------------------------------
// Shader lifecycle
// ---------------------------------------------------------------------------

void PlotEngine2D::ensure_shader_(tgfx2::IRenderDevice& device) {
    if (shader_device_ == &device && shader_vs_id_ != 0) return;

    if (shader_device_) {
        if (shader_vs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_vs_id_;
            shader_device_->destroy(h);
        }
        if (shader_fs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_fs_id_;
            shader_device_->destroy(h);
        }
    }

    tgfx2::ShaderDesc vd;
    vd.stage = tgfx2::ShaderStage::Vertex;
    vd.source = kVert;
    shader_vs_id_ = device.create_shader(vd).id;

    tgfx2::ShaderDesc fd;
    fd.stage = tgfx2::ShaderStage::Fragment;
    fd.source = kFrag;
    shader_fs_id_ = device.create_shader(fd).id;

    shader_device_ = &device;
}

void PlotEngine2D::release_gpu_resources() {
    if (shader_device_) {
        if (shader_vs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_vs_id_;
            shader_device_->destroy(h);
        }
        if (shader_fs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_fs_id_;
            shader_device_->destroy(h);
        }
    }
    shader_vs_id_ = 0;
    shader_fs_id_ = 0;
    shader_device_ = nullptr;
    if (text2d_) text2d_->release_gpu();
}

// ---------------------------------------------------------------------------
// Vertex emitters (scratch-buffer style)
// ---------------------------------------------------------------------------

void PlotEngine2D::emit_rect_tris_(std::vector<float>& v,
                                    float x, float y, float w, float h,
                                    const Color4& c) const {
    // 2 triangles. CCW in pixel y+down visual → after ortho y-flip →
    // CCW in NDC y+up (survives default CullMode::Back).
    // Triangle 1: TL, BL, BR   Triangle 2: TL, BR, TR
    auto push = [&](float px, float py) {
        v.push_back(px);  v.push_back(py);  v.push_back(0.0f);
        v.push_back(c.r); v.push_back(c.g); v.push_back(c.b); v.push_back(c.a);
    };
    const float x0 = x, x1 = x + w;
    const float y0 = y, y1 = y + h;
    push(x0, y0); push(x0, y1); push(x1, y1);
    push(x0, y0); push(x1, y1); push(x1, y0);
}

void PlotEngine2D::emit_rect_outline_lines_(std::vector<float>& v,
                                              float x, float y, float w, float h,
                                              const Color4& c) const {
    // 4 edges. thickness is 1px — no quad expansion.
    const float x0 = x, x1 = x + w;
    const float y0 = y, y1 = y + h;
    emit_line_(v, x0, y0, x1, y0, c);  // top
    emit_line_(v, x1, y0, x1, y1, c);  // right
    emit_line_(v, x1, y1, x0, y1, c);  // bottom
    emit_line_(v, x0, y1, x0, y0, c);  // left
}

void PlotEngine2D::emit_line_(std::vector<float>& v,
                                float x1, float y1, float x2, float y2,
                                const Color4& c) const {
    auto push = [&](float px, float py) {
        v.push_back(px);  v.push_back(py);  v.push_back(0.0f);
        v.push_back(c.r); v.push_back(c.g); v.push_back(c.b); v.push_back(c.a);
    };
    push(x1, y1);
    push(x2, y2);
}

void PlotEngine2D::flush_triangles_(tgfx2::RenderContext2& ctx,
                                      std::vector<float>& v) {
    if (v.empty()) return;
    ctx.draw_immediate_triangles(v.data(), (uint32_t)(v.size() / 7));
    v.clear();
}

void PlotEngine2D::flush_lines_(tgfx2::RenderContext2& ctx,
                                  std::vector<float>& v) {
    if (v.empty()) return;
    ctx.draw_immediate_lines(v.data(), (uint32_t)(v.size() / 7));
    v.clear();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void PlotEngine2D::render(tgfx2::RenderContext2* ctx, tgfx2::FontAtlas* font) {
    if (!ctx || vw_ <= 0 || vh_ <= 0) return;

    ensure_shader_(ctx->device());

    // Render state.
    ctx->set_depth_test(false);
    ctx->set_blend(true);
    ctx->set_cull(tgfx2::CullMode::None);

    // Bind UI shader + ortho projection once per frame. Text2D below
    // rebinds its own shader on every draw, so we must re-bind ours
    // between text and further immediate draws.
    auto bind_ui = [&](tgfx2::RenderContext2& c) {
        tgfx2::ShaderHandle vs; vs.id = shader_vs_id_;
        tgfx2::ShaderHandle fs; fs.id = shader_fs_id_;
        c.bind_shader(vs, fs);
        float proj[16];
        build_ortho((float)vw_, (float)vh_, proj);
        // Because (vx_, vy_) may not be (0, 0) (we're inside a bigger
        // viewport), we need to translate pixel coords into the ortho
        // frame. Simplest: set_viewport to the visible region and use
        // a 0-based ortho. But we need to mirror the translation the
        // Python version implicitly does by mapping (vx_, vy_) to
        // pixel (0, 0). Do it here in the shader projection.
        float tx = -vx_;
        float ty = -vy_;
        proj[3] += 2.0f * tx / std::max(vw_, 1.0f);
        proj[7] += -2.0f * ty / std::max(vh_, 1.0f);
        c.set_uniform_mat4("u_projection", proj, /*transpose=*/true);
    };
    bind_ui(*ctx);

    // Batch #1 — background + plot-area fill (two rects).
    {
        std::vector<float> verts;
        emit_rect_tris_(verts, vx_, vy_, vw_, vh_, bg_color);
        const Rect pa = plot_area_();
        emit_rect_tris_(verts, pa.x, pa.y, pa.w, pa.h, plot_bg_color);
        flush_triangles_(*ctx, verts);
    }

    const Rect pa = plot_area_();
    const ViewRange v = view_range_();

    // Batch #2 — grid inside clip.
    if (show_grid) {
        // Enable scissor to plot area. Scissor is in GL pixel coords
        // (y+ up, origin bottom-left of the framebuffer).
        // Our FBO size is (vw_, vh_) with (vx_, vy_) = top-left; pass
        // set_scissor in window pixel coords: the OpenGL device handles
        // the y-flip internally for us (see OpenGLCommandList).
        ctx->set_scissor((int)pa.x, (int)pa.y, (int)pa.w, (int)pa.h);
        bind_ui(*ctx);  // scissor sometimes drops bound state on some drivers

        const int max_x_ticks = std::max(int(pa.w / 80.0f), 3);
        const int max_y_ticks = std::max(int(pa.h / 50.0f), 3);
        const std::vector<double> x_ticks = axes::nice_ticks(v.x_min, v.x_max, max_x_ticks);
        const std::vector<double> y_ticks = axes::nice_ticks(v.y_min, v.y_max, max_y_ticks);

        std::vector<float> line_verts;
        for (double tx : x_ticks) {
            float sx, _sy;
            data_to_pixel_(tx, 0.0, sx, _sy);
            emit_line_(line_verts, sx, pa.y, sx, pa.y + pa.h, grid_color);
        }
        for (double ty : y_ticks) {
            float _sx, sy;
            data_to_pixel_(0.0, ty, _sx, sy);
            emit_line_(line_verts, pa.x, sy, pa.x + pa.w, sy, grid_color);
        }
        flush_lines_(*ctx, line_verts);
    } else {
        ctx->set_scissor((int)pa.x, (int)pa.y, (int)pa.w, (int)pa.h);
        bind_ui(*ctx);
    }

    // Batch #3 — line series (one draw per series; all segments in
    // one buffer). Fixes the 400-segment = 400-draw-calls perf bug.
    {
        uint32_t palette_i = 0;
        for (const auto& s : data.lines) {
            if (s.x.size() < 2) { palette_i++; continue; }
            const Color4 c = s.color.has_value()
                ? *s.color : styles::cycle_color(palette_i);
            std::vector<float> verts;
            verts.reserve((s.x.size() - 1) * 2 * 7);
            for (size_t i = 0; i + 1 < s.x.size(); i++) {
                float x1, y1, x2, y2;
                data_to_pixel_(s.x[i], s.y[i], x1, y1);
                data_to_pixel_(s.x[i + 1], s.y[i + 1], x2, y2);
                emit_line_(verts, x1, y1, x2, y2, c);
            }
            flush_lines_(*ctx, verts);
            palette_i++;
        }
    }

    // Batch #4 — scatter (squares via triangle pairs).
    {
        uint32_t palette_i = (uint32_t)data.lines.size();
        std::vector<float> verts;
        for (const auto& s : data.scatters) {
            const Color4 c = s.color.has_value()
                ? *s.color : styles::cycle_color(palette_i);
            const float half = (float)s.size * 0.5f;
            for (size_t i = 0; i < s.x.size(); i++) {
                float sx, sy;
                data_to_pixel_(s.x[i], s.y[i], sx, sy);
                emit_rect_tris_(verts, sx - half, sy - half,
                                (float)s.size, (float)s.size, c);
            }
            palette_i++;
        }
        flush_triangles_(*ctx, verts);
    }

    // End inner-area clip.
    ctx->clear_scissor();
    bind_ui(*ctx);

    // Batch #5 — axes border (outside the inner scissor).
    {
        std::vector<float> verts;
        emit_rect_outline_lines_(verts, pa.x, pa.y, pa.w, pa.h, axis_color);
        flush_lines_(*ctx, verts);
    }

    // --- Text: tick labels, title, axis labels ---
    if (font) {
        text2d_->begin(ctx, (int)vw_, (int)vh_, font);

        // Tick labels outside the clip.
        const int max_x_ticks = std::max(int(pa.w / 80.0f), 3);
        const int max_y_ticks = std::max(int(pa.h / 50.0f), 3);
        const std::vector<double> x_ticks = axes::nice_ticks(v.x_min, v.x_max, max_x_ticks);
        const std::vector<double> y_ticks = axes::nice_ticks(v.y_min, v.y_max, max_y_ticks);
        const float tick_sz = font_size - 1.0f;

        // Our Text2DRenderer expects viewport-pixel coords starting at
        // (0, 0) top-left of the viewport. Our engine2d may be offset
        // by (vx_, vy_), but the render-pass' color attachment was
        // already (vw_ x vh_) sized, so (vx_, vy_) is effectively (0,0)
        // within the FBO. Translate accordingly by subtracting.
        auto T_x = [&](float x) { return x - vx_; };
        auto T_y = [&](float y) { return y - vy_; };

        for (double tx : x_ticks) {
            float sx, _sy;
            data_to_pixel_(tx, 0.0, sx, _sy);
            text2d_->draw(axes::format_tick(tx),
                          T_x(sx), T_y(pa.y + pa.h + 14.0f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          tick_sz,
                          tgfx2::Text2DRenderer::Anchor::Center);
        }
        for (double ty : y_ticks) {
            float _sx, sy;
            data_to_pixel_(0.0, ty, _sx, sy);
            const std::string lab = axes::format_tick(ty);
            const auto m = font->measure_text(lab, tick_sz);
            const float tw = m.width;
            text2d_->draw(lab,
                          T_x(pa.x - tw - 6.0f), T_y(sy + 4.0f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          tick_sz,
                          tgfx2::Text2DRenderer::Anchor::Left);
        }

        if (!data.title.empty()) {
            text2d_->draw(data.title,
                          T_x(vx_ + vw_ * 0.5f),
                          T_y(vy_ + margin_top * 0.5f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          title_font_size,
                          tgfx2::Text2DRenderer::Anchor::Center);
        }
        if (!data.x_label.empty()) {
            text2d_->draw(data.x_label,
                          T_x(pa.x + pa.w * 0.5f),
                          T_y(pa.y + pa.h + margin_bottom - 4.0f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          font_size,
                          tgfx2::Text2DRenderer::Anchor::Center);
        }
        if (!data.y_label.empty()) {
            text2d_->draw(data.y_label,
                          T_x(vx_ + margin_left * 0.5f),
                          T_y(pa.y + pa.h * 0.5f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          font_size,
                          tgfx2::Text2DRenderer::Anchor::Center);
        }

        text2d_->end();
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool PlotEngine2D::on_mouse_down(float x, float y, tcbase::MouseButton button) {
    if (button == tcbase::MouseButton::MIDDLE) {
        panning_ = true;
        pan_start_mx_ = x;
        pan_start_my_ = y;
        const ViewRange v = view_range_();
        pan_start_view_[0] = v.x_min;
        pan_start_view_[1] = v.x_max;
        pan_start_view_[2] = v.y_min;
        pan_start_view_[3] = v.y_max;
        return true;
    }
    return false;
}

void PlotEngine2D::on_mouse_move(float x, float y) {
    if (!panning_) return;
    const Rect pa = plot_area_();
    const double vx0 = pan_start_view_[0];
    const double vx1 = pan_start_view_[1];
    const double vy0 = pan_start_view_[2];
    const double vy1 = pan_start_view_[3];
    const double dx_px = x - pan_start_mx_;
    const double dy_px = y - pan_start_my_;
    const double dx_data = -dx_px / pa.w * (vx1 - vx0);
    const double dy_data = dy_px / pa.h * (vy1 - vy0);  // Y flipped
    view_x_min_ = vx0 + dx_data;
    view_x_max_ = vx1 + dx_data;
    view_y_min_ = vy0 + dy_data;
    view_y_max_ = vy1 + dy_data;
}

void PlotEngine2D::on_mouse_up(float /*x*/, float /*y*/,
                                 tcbase::MouseButton /*button*/) {
    panning_ = false;
}

bool PlotEngine2D::on_mouse_wheel(float x, float y, float dy) {
    const Rect pa = plot_area_();
    if (x < pa.x || x > pa.x + pa.w || y < pa.y || y > pa.y + pa.h) return false;

    const float factor = (dy > 0) ? 0.85f : 1.0f / 0.85f;

    double cx, cy;
    pixel_to_data_(x, y, cx, cy);
    const ViewRange v = view_range_();

    view_x_min_ = cx + (v.x_min - cx) * factor;
    view_x_max_ = cx + (v.x_max - cx) * factor;
    view_y_min_ = cy + (v.y_min - cy) * factor;
    view_y_max_ = cy + (v.y_max - cy) * factor;
    return true;
}

}  // namespace tcplot
