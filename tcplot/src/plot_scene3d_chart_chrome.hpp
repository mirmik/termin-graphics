#pragma once

#include <memory>
#include <string>

#include "tcplot/plot_scene3d_render_item_source.hpp"

namespace tgfx {
    class Canvas2DRenderer;
    class FontAtlas;
    class RenderContext2;
} // namespace tgfx

namespace tcplot {

    namespace detail {

        struct PlotScene3DCanvasPoint {
            float x = 0.0f;
            float y = 0.0f;
        };

        inline PlotScene3DCanvasPoint termin_clip_ndc_to_canvas(float ndc_x,
                                                                 float ndc_y,
                                                                 int viewport_width,
                                                                 int viewport_height) noexcept {
            return {
                (ndc_x * 0.5f + 0.5f) * static_cast<float>(viewport_width),
                (ndc_y * 0.5f + 0.5f) * static_cast<float>(viewport_height),
            };
        }

    } // namespace detail

    class PlotScene3DChartChromeRenderer {
    public:
        PlotScene3DChartChromeRenderer();
        ~PlotScene3DChartChromeRenderer();

        PlotScene3DChartChromeRenderer(const PlotScene3DChartChromeRenderer&) = delete;
        PlotScene3DChartChromeRenderer& operator=(const PlotScene3DChartChromeRenderer&) = delete;

        void draw_grid_labels(tgfx::RenderContext2& context,
                              tgfx::FontAtlas& font,
                              const PlotScene3DFrameRenderState& frame,
                              const tc_grid_item3d_style& style,
                              int viewport_width,
                              int viewport_height);

        void draw_colorbar(tgfx::RenderContext2& context,
                           tgfx::FontAtlas& font,
                           const PlotScene3DFrameRenderState& frame,
                           const tc_surface_item3d_style& surface_style,
                           const tc_colorbar3d_style& colorbar_style,
                           const std::string& label,
                           int viewport_width,
                           int viewport_height);

        void release_gpu();

    private:
        std::unique_ptr<tgfx::Canvas2DRenderer> canvas_;
    };

} // namespace tcplot
