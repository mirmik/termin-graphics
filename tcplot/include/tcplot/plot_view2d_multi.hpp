// plot_view2d_multi.hpp - Multi-panel 2D plot view with shared X axis.
//
// Wraps N PlotEngine2D instances that share:
//   - one GL device, render context, font atlas, offscreen FBO
//   - a single "shared X" view range
// Each panel keeps an independent Y range. Suitable for time-series
// dashboards: append new samples per tick, the view optionally
// auto-scrolls to keep the last `window_size` X-units visible, and
// each panel's Y range auto-grows (never shrinks) to fit incoming
// data without user intervention.
//
// Non-owning: `device/cache/ctx/font` are provided by the caller (a
// process-wide Tgfx2Context) and must outlive the view. This mirrors
// the `RenderEngine::ensure_tgfx2` pattern and the Python-side
// `Tgfx2Context.from_window` contract — application-level host owns
// exactly one OpenGLRenderDevice, every renderer borrows it. The old
// self-contained-ctor model (view builds its own device) is gone:
// recreating a device to change panel count caused GL resource
// collisions on shared contexts (two GLWpfControls in one window).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <tgfx2/enums.hpp>
#include <tgfx2/handles.hpp>

#include "tcplot/plot_data.hpp"
#include "tcplot/styles.hpp"
#include "tcplot/tcplot_api.h"

namespace tgfx {
class IRenderDevice;
class PipelineCache;
class RenderContext2;
class FontAtlas;
}

namespace tcplot {

class GpuHost;
class PlotEngine2D;

class TCPLOT_API PlotView2DMulti {
public:
    // Borrow device/cache/ctx/font from the caller. Host must guarantee
    // they outlive this view (typical lifecycle: application-level
    // singleton, process-wide). The view never destroys them.
    PlotView2DMulti(tgfx::IRenderDevice& device,
                    tgfx::PipelineCache& cache,
                    tgfx::RenderContext2& ctx,
                    tgfx::FontAtlas& font,
                    int panel_count);

    // Convenience ctor that pulls all four references from a GpuHost.
    // Prefer this in application code — host owns one GpuHost for the
    // process, every view takes it by reference.
    PlotView2DMulti(GpuHost& host, int panel_count);

    ~PlotView2DMulti();

    PlotView2DMulti(const PlotView2DMulti&) = delete;
    PlotView2DMulti& operator=(const PlotView2DMulti&) = delete;

    int panel_count() const;

    // Resize `panels_` in place. Existing panels up to the new count
    // keep their state (data, Y ranges, titles). Extra panels are
    // appended fresh; panels shed when shrinking release their GL
    // resources before being dropped. Device, cache, ctx, font — all
    // untouched. This is the no-recreate path that replaces the old
    // "dispose view + new view" idiom.
    void set_panel_count(int n);

    // Add a line series to `panel_idx`. Returns index of the series
    // within that panel (0-based), or -1 if panel_idx is out of range.
    int add_line(int panel_idx,
                 const double* x, const double* y, size_t n,
                 float cr, float cg, float cb, float ca,
                 double thickness = 1.5,
                 const char* label = "");

    // Append points to an existing line series. Autoscroll + per-panel
    // Y-autoscale both react to the new data — see set_autoscroll.
    void append_to_line(int panel_idx, int series_idx,
                        const double* x, const double* y, size_t n);

    // Remove all series from all panels. Keeps the panel count and
    // style; callers typically re-add series afterwards.
    void clear();

    // Style / labels.
    void set_panel_title(int panel_idx, const char* title);
    void set_panel_y_label(int panel_idx, const char* label);
    void set_x_label(const char* label);

    // MSAA sample count for the shared offscreen attachment. Same
    // contract as PlotView2D::set_msaa_samples — next render() picks
    // up the new value. Default 4.
    void set_msaa_samples(int samples);

    // --- Shared X / per-panel Y control ---

    // Autoscroll mode: when on, every append that advances the max
    // data X beyond the current window's right edge slides the window
    // so the rightmost visible X == that newest data X, keeping
    // `window_size` X-units visible to the left. Default off.
    void set_autoscroll(bool on, double window_size);

    // Manually set the shared X view (disables autoscroll for the
    // next frame but not permanently — autoscroll_on state stays).
    void set_shared_view_x(double x_min, double x_max);

    // Manually set a panel's Y view. If set, replaces the engine's
    // auto-grow-only state; subsequent append_to_line will again
    // grow-only from this baseline.
    void set_panel_view_y(int panel_idx, double y_min, double y_max);

    // --- Colour + typography overrides (propagated to every panel) ---
    // Strip background (behind margin area), plot-area background, grid,
    // axis, labels. Pass 0..1 RGBA. Useful for matching a host theme.
    void set_bg_color        (float r, float g, float b, float a);
    void set_plot_bg_color   (float r, float g, float b, float a);
    void set_grid_color      (float r, float g, float b, float a);
    void set_axis_color      (float r, float g, float b, float a);
    void set_label_color     (float r, float g, float b, float a);
    // Explicit title colour applied to every panel. Overrides the
    // default (which is the panel's label_color). Typical use: pick a
    // theme-specific title colour once per theme switch.
    void set_title_color     (float r, float g, float b, float a);
    // Reset title colour to default (= each panel's label_color).
    void clear_title_color   ();
    // Change the colour of an already-added series. For theme
    // switching — no need to clear + re-add. Silent no-op if indices
    // are out of range.
    void set_line_color      (int panel_idx, int series_idx,
                              float r, float g, float b, float a);
    // Per-panel font sizes in pixels. Margins auto-scale to fit.
    void set_font_size       (float label_px, float title_px);
    void set_panel_margins   (int left, int right, int top, int bottom);
    // Vertical gap (px) between every panel's title bottom edge and
    // the top of its plot area. See PlotEngine2D::title_pad.
    void set_title_pad       (float pad);

    // --- Virtual scrolling ---
    // Fixed panel height in pixels. When > 0 the view lays panels out
    // on a virtual canvas of (panel_count * panel_height) px, clipped
    // by the actual render height; set_scroll_offset slides the canvas.
    // When 0 (default) the old behavior applies: panels divide the
    // render height equally and no virtualization happens.
    void set_panel_height(float h);

    // Virtual scroll offset in pixels (0 = top of canvas). Clamped
    // to [0, max(total_virtual_height - render_height, 0)] at render
    // time. Host is responsible for driving the actual WPF scrollbar.
    void set_scroll_offset(float offset);

    // Virtual canvas height in pixels (panel_count * panel_height).
    // Returns 0 when panel_height is unset (classic layout).
    float total_virtual_height() const;

    // --- Frame lifecycle ---

    void render(int width, int height, uint32_t dst_gl_fbo);
    void release_gpu();

    // --- Input forwarding ---
    // Mouse events are routed to the panel under the cursor; pan/zoom
    // on X is applied to the shared X view across all panels; pan/zoom
    // on Y is per-panel (via the target panel's engine).
    bool on_mouse_down(float x, float y, int button);
    void on_mouse_move(float x, float y);
    void on_mouse_up(float x, float y, int button);
    bool on_mouse_wheel(float x, float y, float dy);
    // Ctrl+wheel: zoom shared X only, leave per-panel Y alone.
    bool on_mouse_wheel_x(float x, float y, float dy);

private:
    void ensure_offscreen_(int w, int h);
    void blit_to_dst_(int w, int h, uint32_t dst_gl_fbo);

    // Find panel index for a given viewport-y. Returns -1 on miss.
    int  panel_at_(float y) const;
    // Lay out the N panel rects vertically within (w, h).
    void layout_panels_(int w, int h);

    // Keep shared-X view in sync with the current autoscroll state
    // and latest data in any panel. Called once per frame, before
    // individual engines render.
    void update_shared_x_();

    // Borrowed, non-owning. Lifetime is the host's responsibility.
    tgfx::IRenderDevice*  device_ = nullptr;
    tgfx::PipelineCache*  cache_  = nullptr;
    tgfx::RenderContext2* ctx_    = nullptr;
    tgfx::FontAtlas*      font_   = nullptr;

    std::vector<std::unique_ptr<PlotEngine2D>> panels_;

    // Cached panel viewport rects (filled by layout_panels_).
    struct Rect { float x, y, w, h; };
    std::vector<Rect> panel_rects_;

    // --- Shared X view ---
    bool   have_shared_x_ = false;
    double shared_x_min_ = 0.0;
    double shared_x_max_ = 1.0;
    bool   autoscroll_ = false;
    double autoscroll_window_ = 10.0;

    std::string x_label_;

    // Interaction state: which panel owns the current drag, if any.
    int dragging_panel_ = -1;

    // Offscreen + MSAA (same pattern as PlotView2D).
    tgfx::TextureHandle offscreen_color_{};
    int offscreen_w_ = 0;
    int offscreen_h_ = 0;
    int msaa_samples_ = 4;

    // Virtual scrolling state.
    // panel_height_ > 0 enables virtualised layout; 0 falls back to
    // classic "equal strips over full render height".
    float panel_height_  = 0.0f;
    float scroll_offset_ = 0.0f;

    // Cached visibility: indices of panels whose rect intersects the
    // current render area. Filled by layout_panels_; render/mouse use
    // this to skip off-screen panels without touching their engines.
    std::vector<int> visible_panels_;
};

}  // namespace tcplot
