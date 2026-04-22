// plot_view2d_multi.cpp

#include "tcplot/plot_view2d_multi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <tcbase/input_enums.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/engine2d.hpp"
#include "tcplot/gpu_host.hpp"

namespace tcplot {

namespace {
std::vector<double> to_vec(const double* p, size_t n) {
    return (p && n) ? std::vector<double>(p, p + n) : std::vector<double>{};
}
}

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

PlotView2DMulti::PlotView2DMulti(tgfx::IRenderDevice& device,
                                 tgfx::PipelineCache& cache,
                                 tgfx::RenderContext2& ctx,
                                 tgfx::FontAtlas& font,
                                 int panel_count)
    : device_(&device),
      cache_(&cache),
      ctx_(&ctx),
      font_(&font) {
    if (panel_count < 1) panel_count = 1;
    panels_.reserve(panel_count);
    for (int i = 0; i < panel_count; ++i) {
        panels_.emplace_back(std::make_unique<PlotEngine2D>());
    }
}

PlotView2DMulti::PlotView2DMulti(GpuHost& host, int panel_count)
    : PlotView2DMulti(host.device(), host.cache(), host.ctx(), host.font(),
                      panel_count) {}

PlotView2DMulti::~PlotView2DMulti() {
    // Release only what THIS view owns: per-panel GPU state and the
    // offscreen target. device/cache/ctx/font are borrowed — host
    // tears them down with the process.
    release_gpu();
}

int PlotView2DMulti::panel_count() const {
    return static_cast<int>(panels_.size());
}

void PlotView2DMulti::set_panel_count(int n) {
    if (n < 1) n = 1;
    if ((int)panels_.size() == n) return;

    if ((int)panels_.size() > n) {
        // Shrink: release GL resources of panels we're about to drop.
        // PlotEngine2D's destructor does this too via
        // release_gpu_resources(), but doing it explicitly keeps the
        // moment-of-release predictable (right here, with the right
        // GL context current) instead of relying on unique_ptr
        // destruction order.
        for (size_t i = static_cast<size_t>(n); i < panels_.size(); ++i) {
            if (panels_[i]) panels_[i]->release_gpu_resources();
        }
        panels_.resize(static_cast<size_t>(n));
    } else {
        panels_.reserve(static_cast<size_t>(n));
        while ((int)panels_.size() < n) {
            panels_.emplace_back(std::make_unique<PlotEngine2D>());
        }
    }

    // Layout caches are indexed by panel id — drop them so the next
    // render rebuilds against the new size.
    panel_rects_.clear();
    visible_panels_.clear();
    dragging_panel_ = -1;
}

// ---------------------------------------------------------------------------
// Series
// ---------------------------------------------------------------------------

int PlotView2DMulti::add_line(int panel_idx,
                                const double* x, const double* y, size_t n,
                                float cr, float cg, float cb, float ca,
                                double thickness,
                                const char* label) {
    if (panel_idx < 0 || panel_idx >= (int)panels_.size()) return -1;
    PlotEngine2D& eng = *panels_[panel_idx];
    const int new_idx = static_cast<int>(eng.line_count());

    Color4 c{cr, cg, cb, ca};
    eng.plot(to_vec(x, n), to_vec(y, n),
             std::optional<Color4>{c},
             thickness,
             label ? std::string(label) : std::string());

    // Seed shared X with first data we see.
    if (n > 0 && !have_shared_x_) {
        have_shared_x_ = true;
        shared_x_min_ = x[0];
        shared_x_max_ = x[n - 1];
        if (shared_x_max_ <= shared_x_min_) {
            shared_x_max_ = shared_x_min_ + 1.0;
        }
    }
    return new_idx;
}

void PlotView2DMulti::append_to_line(int panel_idx, int series_idx,
                                       const double* x, const double* y,
                                       size_t n) {
    if (panel_idx < 0 || panel_idx >= (int)panels_.size()) return;
    if (n == 0 || !x || !y) return;

    PlotEngine2D& eng = *panels_[panel_idx];
    eng.append_to_line(static_cast<size_t>(series_idx), x, y, n);

    // Grow-only Y autoscale for the target panel. Read the current
    // view, merge with the new-points' min/max, push back. On first
    // data point in a previously-empty series the engine's lazy fit
    // won't have run — seed directly from incoming values.
    double y_min_new = std::numeric_limits<double>::infinity();
    double y_max_new = -std::numeric_limits<double>::infinity();
    double x_max_new = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < n; ++i) {
        if (y[i] < y_min_new) y_min_new = y[i];
        if (y[i] > y_max_new) y_max_new = y[i];
        if (x[i] > x_max_new) x_max_new = x[i];
    }

    double cur_x_min, cur_x_max, cur_y_min, cur_y_max;
    eng.get_view(cur_x_min, cur_x_max, cur_y_min, cur_y_max);
    const double merged_y_min = std::min(cur_y_min, y_min_new);
    const double merged_y_max = std::max(cur_y_max, y_max_new);
    if (merged_y_max > merged_y_min) {
        eng.set_view_y(merged_y_min, merged_y_max);
    }

    // Grow shared X (monotonic rightward). If autoscroll is on, the
    // update_shared_x_ step at render time will slide the left edge.
    if (!have_shared_x_) {
        have_shared_x_ = true;
        shared_x_min_ = x[0];
        shared_x_max_ = x_max_new;
    } else {
        if (x_max_new > shared_x_max_) shared_x_max_ = x_max_new;
    }
}

void PlotView2DMulti::clear() {
    for (auto& p : panels_) p->clear();
    have_shared_x_ = false;
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

void PlotView2DMulti::set_panel_title(int panel_idx, const char* title) {
    if (panel_idx < 0 || panel_idx >= (int)panels_.size()) return;
    panels_[panel_idx]->data.title = title ? title : "";
}

void PlotView2DMulti::set_panel_y_label(int panel_idx, const char* label) {
    if (panel_idx < 0 || panel_idx >= (int)panels_.size()) return;
    panels_[panel_idx]->data.y_label = label ? label : "";
}

void PlotView2DMulti::set_x_label(const char* label) {
    x_label_ = label ? label : "";
    // Hang the x_label on the bottom panel so it's rendered there.
    if (!panels_.empty()) {
        panels_.back()->data.x_label = x_label_;
    }
}

void PlotView2DMulti::set_msaa_samples(int samples) {
    if (samples < 1) samples = 1;
    if (samples == msaa_samples_) return;
    msaa_samples_ = samples;
    if (device_ && offscreen_color_.id != 0) {
        device_->destroy(offscreen_color_);
    }
    offscreen_color_ = tgfx::TextureHandle{};
    offscreen_w_ = 0;
    offscreen_h_ = 0;
}

// ---------------------------------------------------------------------------
// Shared X / Y control
// ---------------------------------------------------------------------------

void PlotView2DMulti::set_autoscroll(bool on, double window_size) {
    autoscroll_ = on;
    if (window_size > 0) autoscroll_window_ = window_size;
}

void PlotView2DMulti::set_shared_view_x(double x_min, double x_max) {
    have_shared_x_ = true;
    shared_x_min_ = x_min;
    shared_x_max_ = x_max;
}

void PlotView2DMulti::set_panel_view_y(int panel_idx,
                                        double y_min, double y_max) {
    if (panel_idx < 0 || panel_idx >= (int)panels_.size()) return;
    panels_[panel_idx]->set_view_y(y_min, y_max);
}

// ---------------------------------------------------------------------------
// Colour + typography overrides
// ---------------------------------------------------------------------------

void PlotView2DMulti::set_bg_color(float r, float g, float b, float a) {
    for (auto& p : panels_) p->bg_color = {r, g, b, a};
}
void PlotView2DMulti::set_plot_bg_color(float r, float g, float b, float a) {
    for (auto& p : panels_) p->plot_bg_color = {r, g, b, a};
}
void PlotView2DMulti::set_grid_color(float r, float g, float b, float a) {
    for (auto& p : panels_) p->grid_color = {r, g, b, a};
}
void PlotView2DMulti::set_axis_color(float r, float g, float b, float a) {
    for (auto& p : panels_) p->axis_color = {r, g, b, a};
}
void PlotView2DMulti::set_label_color(float r, float g, float b, float a) {
    for (auto& p : panels_) p->label_color = {r, g, b, a};
}
void PlotView2DMulti::set_font_size(float label_px, float title_px) {
    for (auto& p : panels_) {
        p->font_size = label_px;
        p->title_font_size = title_px;
    }
}
void PlotView2DMulti::set_panel_margins(int left, int right, int top, int bottom) {
    for (auto& p : panels_) {
        p->margin_left = left;
        p->margin_right = right;
        p->margin_top = top;
        p->margin_bottom = bottom;
    }
}
void PlotView2DMulti::set_title_pad(float pad) {
    for (auto& p : panels_) p->title_pad = pad;
}

// ---------------------------------------------------------------------------
// Virtual scrolling
// ---------------------------------------------------------------------------

void PlotView2DMulti::set_panel_height(float h) {
    panel_height_ = (h > 0.0f) ? h : 0.0f;
}

void PlotView2DMulti::set_scroll_offset(float offset) {
    scroll_offset_ = (offset > 0.0f) ? offset : 0.0f;
}

float PlotView2DMulti::total_virtual_height() const {
    if (panel_height_ <= 0.0f) return 0.0f;
    return panel_height_ * static_cast<float>(panels_.size());
}

void PlotView2DMulti::update_shared_x_() {
    if (!have_shared_x_) return;

    if (autoscroll_) {
        // Keep the window's right edge pinned to shared_x_max_
        // (which append_to_line grew as data arrived).
        shared_x_min_ = shared_x_max_ - autoscroll_window_;
        if (shared_x_min_ >= shared_x_max_) {
            shared_x_min_ = shared_x_max_ - 1.0;
        }
    }

    for (auto& eng : panels_) {
        eng->set_view_x(shared_x_min_, shared_x_max_);
    }
}

// ---------------------------------------------------------------------------
// Offscreen management — mirrors PlotView2D exactly.
// ---------------------------------------------------------------------------

void PlotView2DMulti::ensure_offscreen_(int w, int h) {
    if (offscreen_color_.id != 0 && offscreen_w_ == w && offscreen_h_ == h) return;
    if (offscreen_color_.id != 0) device_->destroy(offscreen_color_);
    offscreen_color_ = tgfx::TextureHandle{};

    tgfx::TextureDesc desc;
    desc.width  = static_cast<uint32_t>(w);
    desc.height = static_cast<uint32_t>(h);
    desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    desc.usage  = tgfx::TextureUsage::Sampled
                | tgfx::TextureUsage::ColorAttachment
                | tgfx::TextureUsage::CopySrc;
    desc.sample_count = static_cast<uint32_t>(msaa_samples_);
    offscreen_color_ = device_->create_texture(desc);

    offscreen_w_ = w;
    offscreen_h_ = h;
}

void PlotView2DMulti::blit_to_dst_(int w, int h, uint32_t dst_gl_fbo) {
    device_->blit_to_external_target(static_cast<uintptr_t>(dst_gl_fbo),
                                     offscreen_color_,
                                     0, 0, w, h,
                                     0, 0, w, h);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void PlotView2DMulti::layout_panels_(int w, int h) {
    panel_rects_.resize(panels_.size());
    visible_panels_.clear();
    if (panels_.empty() || h <= 0 || w <= 0) return;

    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);

    if (panel_height_ > 0.0f) {
        // Virtualised layout: panels sit on a virtual canvas of
        // (N * panel_height_) px; scroll_offset_ slides the canvas
        // upward. Clamp the offset so we can't scroll past the end.
        const float total = panel_height_ * static_cast<float>(panels_.size());
        const float max_off = std::max(0.0f, total - fh);
        if (scroll_offset_ > max_off) scroll_offset_ = max_off;

        for (size_t i = 0; i < panels_.size(); ++i) {
            Rect r;
            r.x = 0.0f;
            r.y = std::floor(i * panel_height_ - scroll_offset_);
            r.w = fw;
            r.h = std::floor(panel_height_);
            panel_rects_[i] = r;
            // Skip panels fully outside the viewport.
            if (r.y + r.h <= 0.0f || r.y >= fh) continue;
            visible_panels_.push_back(static_cast<int>(i));
        }
    } else {
        // Classic: equal-height strips filling the full render height.
        const float panel_h = fh / panels_.size();
        float y = 0.0f;
        for (size_t i = 0; i < panels_.size(); ++i) {
            Rect r;
            r.x = 0.0f;
            r.y = std::floor(y);
            r.w = fw;
            r.h = (i + 1 == panels_.size())
                ? (fh - r.y)
                : std::floor(y + panel_h) - r.y;
            panel_rects_[i] = r;
            visible_panels_.push_back(static_cast<int>(i));
            y += panel_h;
        }
    }
}

int PlotView2DMulti::panel_at_(float y) const {
    // Walk only the currently visible panels — in virtualised mode
    // off-screen rects have stale y values we shouldn't hit-test.
    for (int idx : visible_panels_) {
        const Rect& r = panel_rects_[idx];
        if (y >= r.y && y < r.y + r.h) return idx;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void PlotView2DMulti::render(int width, int height, uint32_t dst_gl_fbo) {
    if (width <= 0 || height <= 0 || panels_.empty()) return;

    ensure_offscreen_(width, height);
    layout_panels_(width, height);
    update_shared_x_();

    ctx_->begin_frame();

    // Clear to the first panel's bg_color so host overrides via
    // set_bg_color are visible even in the gaps between panel rects
    // (virtualised scrolling leaves a strip at the bottom when fewer
    // panels fit than PanelCount). Fallback to the style default for
    // an empty panel list.
    const Color4 bg = panels_.empty() ? styles::bg_color() : panels_[0]->bg_color;
    const float clear_col[4] = {bg.r, bg.g, bg.b, bg.a};
    ctx_->begin_pass(offscreen_color_, tgfx::TextureHandle{},
                     clear_col, 1.0f, /*clear_depth_enabled=*/false);

    // Only render panels whose rect intersects the viewport. In
    // virtualised mode this is usually 2-6 out of N; the rest pay
    // nothing per frame (no VBO ensure, no text pass, no draw calls).
    for (int i : visible_panels_) {
        PlotEngine2D& eng = *panels_[i];
        const Rect& r = panel_rects_[i];
        eng.set_viewport(r.x, r.y, r.w, r.h);
        // Tell the engine the total FBO height so it can flip y for
        // glViewport/glScissor (engine's (vx_,vy_) are WPF-y top-down,
        // GL wants y from bottom — without this all strips render
        // upside-down relative to panel_at_/layout).
        eng.set_fbo_height(static_cast<float>(height));
        // Make sure the engine uses the shared X (update_shared_x_
        // already pushed it; if no data yet, leave engine at auto-fit).
        eng.render(ctx_, font_);
    }

    ctx_->end_pass();
    ctx_->end_frame();

    // Do NOT defer_destroy offscreen_color_: the texture is owned by
    // this view and persists across frames. defer_destroy would run
    // at end_frame(), wiping the HandlePool entry (and its GL id),
    // and the next render() would then begin_pass() on a dead handle
    // — cue black screen. PlotView2D/3D follow the same convention;
    // release happens in release_gpu() when the view itself dies or
    // the viewport resizes (ensure_offscreen_ handles both).
    blit_to_dst_(width, height, dst_gl_fbo);
}

void PlotView2DMulti::release_gpu() {
    // Only this view's own GPU state: per-panel engine resources and
    // the offscreen target. device/cache/ctx/font are borrowed —
    // NEVER release them here (font in particular: a sibling view
    // may still be using the same FontAtlas through the host's
    // Tgfx2Context).
    for (auto& p : panels_) p->release_gpu_resources();

    if (device_ && offscreen_color_.id != 0) {
        device_->destroy(offscreen_color_);
    }
    offscreen_color_ = tgfx::TextureHandle{};
    offscreen_w_ = 0;
    offscreen_h_ = 0;
}

// ---------------------------------------------------------------------------
// Input routing
// ---------------------------------------------------------------------------

bool PlotView2DMulti::on_mouse_down(float x, float y, int button) {
    const int idx = panel_at_(y);
    if (idx < 0) return false;
    dragging_panel_ = idx;
    PlotEngine2D& eng = *panels_[idx];
    // Engine expects viewport-local coords; set_viewport set the
    // rect in layout_panels_, and engine.on_mouse_* use those internally.
    return eng.on_mouse_down(x, y, static_cast<tcbase::MouseButton>(button));
}

void PlotView2DMulti::on_mouse_move(float x, float y) {
    const int target = (dragging_panel_ >= 0)
        ? dragging_panel_
        : panel_at_(y);
    if (target < 0) return;
    PlotEngine2D& eng = *panels_[target];
    eng.on_mouse_move(x, y);

    // After panning, the engine has a fresh view_x that we treat as
    // the new shared X. Broadcast to other panels.
    double x_min, x_max, y_min, y_max;
    eng.get_view(x_min, x_max, y_min, y_max);
    if (std::abs(x_min - shared_x_min_) > 1e-12
        || std::abs(x_max - shared_x_max_) > 1e-12) {
        shared_x_min_ = x_min;
        shared_x_max_ = x_max;
        have_shared_x_ = true;
        // Any pan is an intent to stop auto-scroll.
        autoscroll_ = false;
        for (size_t i = 0; i < panels_.size(); ++i) {
            if ((int)i == target) continue;
            panels_[i]->set_view_x(x_min, x_max);
        }
    }
}

void PlotView2DMulti::on_mouse_up(float x, float y, int button) {
    const int target = (dragging_panel_ >= 0) ? dragging_panel_ : panel_at_(y);
    if (target >= 0) {
        panels_[target]->on_mouse_up(x, y,
                                      static_cast<tcbase::MouseButton>(button));
    }
    dragging_panel_ = -1;
}

bool PlotView2DMulti::on_mouse_wheel(float x, float y, float dy) {
    const int idx = panel_at_(y);
    if (idx < 0) return false;
    PlotEngine2D& eng = *panels_[idx];
    const bool handled = eng.on_mouse_wheel(x, y, dy);
    if (!handled) return false;

    // Wheel is zoom-around-cursor; the engine updated its view X.
    // Broadcast shared X to siblings so they stay locked.
    double x_min, x_max, y_min, y_max;
    eng.get_view(x_min, x_max, y_min, y_max);
    shared_x_min_ = x_min;
    shared_x_max_ = x_max;
    have_shared_x_ = true;
    autoscroll_ = false;
    for (size_t i = 0; i < panels_.size(); ++i) {
        if ((int)i == idx) continue;
        panels_[i]->set_view_x(x_min, x_max);
    }
    return true;
}

bool PlotView2DMulti::on_mouse_wheel_x(float x, float y, float dy) {
    // Same as on_mouse_wheel but the hit-panel only zooms its X; Y
    // stays put. Used for Ctrl+wheel in the shared-X dashboard UX.
    const int idx = panel_at_(y);
    if (idx < 0) return false;
    PlotEngine2D& eng = *panels_[idx];
    const bool handled = eng.on_mouse_wheel_x(x, y, dy);
    if (!handled) return false;

    double x_min, x_max, y_min, y_max;
    eng.get_view(x_min, x_max, y_min, y_max);
    shared_x_min_ = x_min;
    shared_x_max_ = x_max;
    have_shared_x_ = true;
    autoscroll_ = false;
    for (size_t i = 0; i < panels_.size(); ++i) {
        if ((int)i == idx) continue;
        panels_[i]->set_view_x(x_min, x_max);
    }
    return true;
}

}  // namespace tcplot
