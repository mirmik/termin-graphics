// plot_view2d.hpp - 2D plot view.
//
// Parallel to PlotView3D but for PlotEngine2D. No depth attachment
// needed — 2D compositing is pure alpha-blended triangles + lines.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

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

class PlotEngine2D;
class GpuHost;

class TCPLOT_API PlotView2D {
public:
    PlotView2D(tgfx::IRenderDevice& device,
               tgfx::PipelineCache& cache,
               tgfx::RenderContext2& ctx,
               tgfx::FontAtlas& font);
    explicit PlotView2D(GpuHost& host);
    ~PlotView2D();

    PlotView2D(const PlotView2D&) = delete;
    PlotView2D& operator=(const PlotView2D&) = delete;

    void plot(SeriesData2DView series, LinePlotOptions options = {});

    void plot_colormap(SeriesData2DView series,
                       const double* scalar,
                       LineColormapOptions options = {});

    void scatter(SeriesData2DView series, ScatterPlotOptions options = {});

    void clear();
    void fit();
    void set_view(double x_min, double x_max, double y_min, double y_max);

    void set_title(const char* title);
    void set_x_label(const char* label);
    void set_y_label(const char* label);
    bool set_line_color(int idx, float r, float g, float b, float a);
    bool set_scatter_color(int idx, float r, float g, float b, float a);
    bool set_line_style(int idx, LineStyle style,
                        float dash_px = 8.0f,
                        float gap_px = 5.0f);
    bool set_line_colormap_reversed(int idx, bool reversed);

    bool on_mouse_down(float x, float y, int button);
    void on_mouse_move(float x, float y);
    void on_mouse_up(float x, float y, int button);
    bool on_mouse_wheel(float x, float y, float dy);

    // MSAA sample count for the offscreen color attachment. See
    // plot_view3d.hpp for the contract. Default 4.
    void set_msaa_samples(int samples);
    int  msaa_samples() const { return msaa_samples_; }

    tgfx::TextureHandle render_to_texture(int width, int height);
    uint32_t render_to_texture_id(int width, int height);
    tgfx::TextureHandle color_texture() const { return offscreen_color_; }

    void release_gpu();

private:
    void ensure_offscreen_(int w, int h);

    tgfx::IRenderDevice*  device_ = nullptr;
    tgfx::PipelineCache*  cache_  = nullptr;
    tgfx::RenderContext2* ctx_    = nullptr;
    tgfx::FontAtlas*      font_   = nullptr;
    std::unique_ptr<PlotEngine2D> engine_;

    tgfx::TextureHandle offscreen_color_{};
    int offscreen_w_ = 0;
    int offscreen_h_ = 0;
    int msaa_samples_ = 4;
};

}  // namespace tcplot
