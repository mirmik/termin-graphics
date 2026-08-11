#pragma once

#include <memory>
#include <string>

#include "tcplot/plot_scene3d_render_item_source.hpp"

namespace tgfx {
    class Canvas2DRenderer;
    class FontAtlas;
    class RenderContext2;
    class Text3DRenderer;
} // namespace tgfx

namespace tcplot {

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
        std::unique_ptr<tgfx::Text3DRenderer> text_;
        std::unique_ptr<tgfx::Canvas2DRenderer> canvas_;
    };

} // namespace tcplot
