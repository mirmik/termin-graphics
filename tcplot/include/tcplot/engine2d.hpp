// engine2d.hpp - Host-agnostic 2D plot engine for tcplot.
//
// Port of tcplot/tcplot/engine2d.py. Renders straight through a
// tgfx::RenderContext2 without any tcgui dependency — a small
// private UI shader handles rects/lines, Text2DRenderer handles
// labels. One key behavioural improvement over the Python version:
// every line series emits a single draw_immediate_lines call, so
// the N-point-curve-with-N-draws perf bug is gone from the start.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <tcbase/input_enums.hpp>
#include <tgfx2/handles.hpp>

#include "tcplot/plot_data.hpp"
#include "tcplot/styles.hpp"
#include "tcplot/tcplot_api.h"

namespace tgfx {
class RenderContext2;
class IRenderDevice;
class FontAtlas;
class Text2DRenderer;
}  // namespace tgfx

namespace tcplot {

class TCPLOT_API PlotEngine2D {
public:
    PlotData data;

    // Inner-rect margins, in pixels (left, right, top, bottom).
    // Sized around the default font metrics below — if a host cranks
    // font_size up these should grow too, otherwise tick labels collide
    // with the plot border.
    int margin_left = 76;
    int margin_right = 15;
    int margin_top = 44;
    int margin_bottom = 52;

    // Style
    bool show_grid = true;
    Color4 grid_color = styles::grid_color();
    Color4 axis_color = styles::axis_color();
    Color4 label_color = styles::label_color();
    Color4 bg_color = styles::bg_color();
    Color4 plot_bg_color = styles::plot_area_bg();
    // Pixel sizes follow common dashboard-typography norms: axis labels
    // ≈ 15 px (tick labels -2 → 13 px), title at 22 px so it reads as
    // a header rather than another label (ratio ~1.7× to ticks).
    float font_size = 15.0f;
    float title_font_size = 22.0f;
    // Vertical gap (px) between the title's bottom edge and the top
    // of the plot area. Matches matplotlib's `axes.titlepad`. Bumping
    // this gives the title more breathing room; a negative value would
    // push the title into the plot area (don't).
    float title_pad = 4.0f;

    PlotEngine2D();
    ~PlotEngine2D();

    PlotEngine2D(const PlotEngine2D&) = delete;
    PlotEngine2D& operator=(const PlotEngine2D&) = delete;

    // --- Viewport (host-supplied pixel rect; origin top-left, y+ down) ---
    void set_viewport(float x, float y, float width, float height);

    // Total FBO height in pixels. Required for multi-strip rendering:
    // the engine's (vx_, vy_) rect is specified in y+down coords, but
    // glViewport / glScissor want y measured from the FBO bottom, so
    // we need to know the total height to flip. If left at 0 (default),
    // the engine assumes single-strip rendering (vy_=0, vh_=fbo height),
    // i.e. no flip is applied and behavior matches PlotView2D.
    void set_fbo_height(float h);

    // --- Series API ---
    void plot(std::vector<double> x, std::vector<double> y,
              std::optional<Color4> color = std::nullopt,
              double thickness = 1.5,
              std::string label = "");

    void scatter(std::vector<double> x, std::vector<double> y,
                 std::optional<Color4> color = std::nullopt,
                 double size = 4.0,
                 std::string label = "");

    void clear();

    // Auto-fit the view range to cover all data with ~5% padding.
    void fit();

    // Explicit view range in data coords.
    void set_view(double x_min, double x_max, double y_min, double y_max);

    // --- Time-series / streaming API ---
    //
    // Append `n` points to an existing line series (indexed by the
    // order they were added via plot()). Data-space coords; stored in
    // the series' x/y vectors and uploaded to the tail of the series'
    // persistent VBO on the next render(). Silent no-op if idx is out
    // of range.
    void append_to_line(size_t idx, const double* x, const double* y, size_t n);

    size_t line_count() const { return data.lines.size(); }

    // Expose the last X of an existing line series (for autoscroll
    // logic at the host level). Returns false if idx is out of range
    // or the series is empty.
    bool last_x_of_line(size_t idx, double& out_x) const;

    // Read the current view range (auto-fits on first access). Mostly
    // for shared-X coordination across multi-panel layouts.
    void get_view(double& x_min, double& x_max,
                  double& y_min, double& y_max);
    // Set only the X part of the view; Y left unchanged.
    void set_view_x(double x_min, double x_max);
    void set_view_y(double y_min, double y_max);

    // --- Rendering ---
    //
    // Host passes an active RenderContext2 (inside its own pass) plus
    // a FontAtlas for labels. The engine leaves scissor + shader state
    // unspecified on return.
    void render(tgfx::RenderContext2* ctx, tgfx::FontAtlas* font);

    // Release GPU resources (shader handles). Safe after device
    // teardown.
    void release_gpu_resources();

    // --- Input handlers ---
    bool on_mouse_down(float x, float y, tcbase::MouseButton button);
    void on_mouse_move(float x, float y);
    void on_mouse_up(float x, float y, tcbase::MouseButton button);
    bool on_mouse_wheel(float x, float y, float dy);
    // Zoom X axis only (shared-X multi-panel UX: Ctrl+wheel).
    bool on_mouse_wheel_x(float x, float y, float dy);

private:
    // Plot area in viewport pixel coords.
    struct Rect { float x, y, w, h; };
    Rect plot_area_() const;
    // Current view range; auto-fits on first access if unset.
    struct ViewRange { double x_min, x_max, y_min, y_max; };
    ViewRange view_range_();
    // data ↔ pixel transforms.
    void data_to_pixel_(double dx, double dy, float& out_x, float& out_y);
    void pixel_to_data_(float wx, float wy, double& out_x, double& out_y);

    // Build a pos+color ortho shader on the given device. Cached.
    void ensure_shader_(tgfx::IRenderDevice& device);

    // Build a data-space-only VS + uniform-color FS shader used for
    // persistent-VBO line series. Cached per device.
    void ensure_line_shader_(tgfx::IRenderDevice& device);

    // Ensure series `idx` has a GPU buffer big enough for its current
    // point count and that the tail (gpu_count..x.size()) has been
    // uploaded. Grows the VBO (doubling) when capacity is exceeded.
    void ensure_line_gpu_(tgfx::IRenderDevice& device, size_t idx);

    // Compose the data-space → NDC matrix (4x4 column-major) for the
    // current plot area, view range, and viewport. Used as a uniform
    // by the line shader — panning / zooming only changes this matrix.
    void compute_data_to_clip_(float out16[16]);

    // Draw helpers — each collects verts into a scratch buffer the
    // caller owns, lets the caller issue one ctx.draw_immediate_*()
    // call. This is the antidote to the "399 segments = 399 draws"
    // problem in the Python version.
    void emit_rect_tris_(std::vector<float>& verts,
                         float x, float y, float w, float h,
                         const Color4& c) const;
    void emit_rect_outline_lines_(std::vector<float>& verts,
                                   float x, float y, float w, float h,
                                   const Color4& c) const;
    void emit_line_(std::vector<float>& verts,
                    float x1, float y1, float x2, float y2,
                    const Color4& c) const;

    void flush_triangles_(tgfx::RenderContext2& ctx, std::vector<float>& verts);
    void flush_lines_(tgfx::RenderContext2& ctx, std::vector<float>& verts);

    // --- Viewport rect ---
    float vx_ = 0.0f, vy_ = 0.0f, vw_ = 0.0f, vh_ = 0.0f;
    // 0 = unset → treat as single-strip (no y-flip for GL). Otherwise
    // used to compute glViewport/glScissor y from bottom.
    float fbo_height_ = 0.0f;

    // --- View range (nullopt = auto-fit on first use) ---
    std::optional<double> view_x_min_;
    std::optional<double> view_x_max_;
    std::optional<double> view_y_min_;
    std::optional<double> view_y_max_;

    // --- Pan state ---
    bool panning_ = false;
    float pan_start_mx_ = 0.0f;
    float pan_start_my_ = 0.0f;
    double pan_start_view_[4] = {0, 1, 0, 1};

    // --- Cached shader (rects/grid/scatter — pos+color, ortho pixel). ---
    tgfx::IRenderDevice* shader_device_ = nullptr;
    uint32_t shader_vs_id_ = 0;
    uint32_t shader_fs_id_ = 0;

    // --- Cached shader (line series — data-space pos + uniform color). ---
    tgfx::IRenderDevice* line_shader_device_ = nullptr;
    uint32_t line_shader_vs_id_ = 0;
    uint32_t line_shader_fs_id_ = 0;

    // Per-line-series persistent GPU state, parallel to `data.lines`.
    // capacity is measured in vertices; each vertex is a vec2 float
    // (8 bytes). gpu_count <= x.size(); the render path tops it up
    // with glBufferSubData to the tail on each frame.
    struct LineGpuState {
        tgfx::BufferHandle vbo{};
        uint32_t capacity = 0;
        uint32_t gpu_count = 0;
    };
    std::vector<LineGpuState> line_gpu_;

    // --- Text renderer (unique_ptr to keep header free of Text2D include) ---
    std::unique_ptr<tgfx::Text2DRenderer> text2d_;
};

}  // namespace tcplot
