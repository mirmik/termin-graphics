// engine2d.hpp - Host-agnostic 2D plot engine for tcplot.
//
// Port of tcplot/tcplot/engine2d.py. Renders straight through a
// tgfx::RenderContext2 without any tcgui dependency. Shared
// tgfx::Canvas2DRenderer handles plot chrome and labels; large line
// series use a dedicated persistent-VBO path. One key behavioural
// improvement over the Python version: every line series emits a
// single draw call, so
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
#include "tcplot/plot_frame2d.hpp"
#include "tcplot/styles.hpp"
#include "tcplot/tcplot_api.h"

namespace tgfx {
    class RenderContext2;
    class IRenderDevice;
    class FontAtlas;
    class Canvas2DRenderer;
} // namespace tgfx

namespace tcplot {

    class PlotAnnotationLayer2D;
    class PlotLineSeriesGpu2D;
    class PlotScatterSeriesGpu2D;

    enum class PlotInputResult2D {
        Unhandled,
        Annotation,
        Navigation,
    };

    class TCPLOT_API PlotEngine2D {
    private:
        float vx_ = 0.0f, vy_ = 0.0f, vw_ = 0.0f, vh_ = 0.0f;
        float fbo_height_ = 0.0f;
        float pixel_scale_ = 1.0f;
        std::optional<double> view_x_min_, view_x_max_, view_y_min_, view_y_max_;
        bool panning_ = false;
        float pan_start_mx_ = 0.0f, pan_start_my_ = 0.0f;
        double pan_start_view_[4] = {0, 1, 0, 1};
        std::vector<std::unique_ptr<PlotLineSeriesGpu2D>> line_renderers_;
        std::vector<std::unique_ptr<PlotScatterSeriesGpu2D>> scatter_renderers_;
        std::unique_ptr<tgfx::Canvas2DRenderer> canvas_;
        std::unique_ptr<PlotAnnotationLayer2D> annotations_;
        PlotRenderPhaseSink2D* render_phase_sink_ = nullptr;

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
        SrgbColor grid_color = styles::grid_color();
        SrgbColor axis_color = styles::axis_color();
        SrgbColor label_color = styles::label_color();
        SrgbColor bg_color = styles::bg_color();
        SrgbColor plot_bg_color = styles::plot_area_bg();
        // Explicit title colour. nullopt = use label_color. Earlier
        // revisions auto-picked the first line series' colour (so stacked
        // panels read as "sin is blue, cos is orange"), but that coupling
        // to series state made theming unpredictable — titles flickered
        // when series were re-added, and the "auto" colour couldn't be
        // overridden without clearing the whole panel. Now the default is
        // predictable (label_color); callers who want series-coupled
        // titles can sample the series colour themselves and pass it in.
        std::optional<SrgbColor> title_color;
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

        // Logical-to-physical pixel ratio captured by plot_frame(). Plot
        // coordinates remain in physical viewport pixels.
        void set_pixel_scale(float scale);

        // --- Series API ---
        void plot(std::vector<double> x, std::vector<double> y, LinePlotOptions options = {});

        void plot_colormap(std::vector<double> x,
                           std::vector<double> y,
                           std::vector<double> scalar,
                           LineColormapOptions options = {});

        void scatter(std::vector<double> x, std::vector<double> y, ScatterPlotOptions options = {});

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

        // Replace the colour of an existing line / scatter series. Returns
        // false if idx is out of range. Intended for theme switching —
        // callers don't need to clear + re-add to recolour a live series.
        bool set_line_color(size_t idx, SrgbColor color);
        bool set_scatter_color(size_t idx, SrgbColor color);
        bool set_line_style(size_t idx, LineStyle style, float dash_px = 8.0f, float gap_px = 5.0f);
        bool set_line_colormap_reversed(size_t idx, bool reversed);

        size_t line_count() const {
            return data.lines.size();
        }

        // Expose the last X of an existing line series (for autoscroll
        // logic at the host level). Returns false if idx is out of range
        // or the series is empty.
        bool last_x_of_line(size_t idx, double& out_x) const;

        // Read the current view range (auto-fits on first access). Mostly
        // for shared-X coordination across multi-panel layouts.
        void get_view(double& x_min, double& x_max, double& y_min, double& y_max);
        // Set only the X part of the view; Y left unchanged.
        void set_view_x(double x_min, double x_max);
        void set_view_y(double y_min, double y_max);

        // Immutable projection snapshot for this engine's current viewport and
        // range. Auto-fits on first access when no explicit range was supplied.
        PlotFrame2D plot_frame();
        PlotAnnotationLayer2D& annotations();
        const PlotAnnotationLayer2D& annotations() const;

        // Borrowed advanced render extension. Passing nullptr detaches it.
        void set_render_phase_sink(PlotRenderPhaseSink2D* sink) {
            render_phase_sink_ = sink;
        }
        PlotRenderPhaseSink2D* render_phase_sink() const {
            return render_phase_sink_;
        }

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
        PlotInputResult2D on_mouse_wheel_result(float x, float y, float dy);
        PlotInputResult2D on_mouse_wheel_x_result(float x, float y, float dy);

    private:
        void begin_render_phase_(PlotRenderPhase2D phase,
                                 const PlotFrame2D& frame,
                                 tgfx::RenderContext2& context,
                                 tgfx::FontAtlas* font);
        PlotInputResult2D on_mouse_wheel_impl_(float x, float y, float dy, bool x_only);
    };

} // namespace tcplot
