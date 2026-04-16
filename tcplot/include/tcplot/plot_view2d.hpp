// plot_view2d.hpp - Self-contained 2D plot view.
//
// Parallel to PlotView3D but for PlotEngine2D. No depth attachment
// needed — 2D compositing is pure alpha-blended triangles + lines.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "tcplot/plot_data.hpp"
#include "tcplot/styles.hpp"
#include "tcplot/tcplot_api.h"

namespace tgfx2 {
class OpenGLRenderDevice;
class PipelineCache;
class RenderContext2;
class FontAtlas;
}

namespace tcplot {

class PlotEngine2D;

class TCPLOT_API PlotView2D {
public:
    explicit PlotView2D(const std::string& ttf_path);
    ~PlotView2D();

    PlotView2D(const PlotView2D&) = delete;
    PlotView2D& operator=(const PlotView2D&) = delete;

    void plot(const double* x, const double* y, size_t n,
              float cr, float cg, float cb, float ca,
              double thickness = 1.5,
              const char* label = "");

    void scatter(const double* x, const double* y, size_t n,
                 float cr, float cg, float cb, float ca,
                 double size = 4.0,
                 const char* label = "");

    void clear();
    void fit();
    void set_view(double x_min, double x_max, double y_min, double y_max);

    void set_title(const char* title);
    void set_x_label(const char* label);
    void set_y_label(const char* label);

    bool on_mouse_down(float x, float y, int button);
    void on_mouse_move(float x, float y);
    void on_mouse_up(float x, float y, int button);
    bool on_mouse_wheel(float x, float y, float dy);

    void render(int width, int height, uint32_t dst_gl_fbo);
    void release_gpu();

private:
    void ensure_offscreen_(int w, int h);
    void blit_to_dst_(int w, int h, uint32_t dst_gl_fbo);

    std::string ttf_path_;

    std::unique_ptr<tgfx2::OpenGLRenderDevice> device_;
    std::unique_ptr<tgfx2::PipelineCache> cache_;
    std::unique_ptr<tgfx2::RenderContext2> ctx_;
    std::unique_ptr<tgfx2::FontAtlas> font_;
    std::unique_ptr<PlotEngine2D> engine_;

    uint32_t offscreen_fbo_ = 0;
    uint32_t offscreen_color_tex_ = 0;
    int offscreen_w_ = 0;
    int offscreen_h_ = 0;
};

}  // namespace tcplot
