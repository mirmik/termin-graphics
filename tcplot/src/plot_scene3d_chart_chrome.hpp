#pragma once

#include <memory>

#include "tcplot/plot_scene3d_render_item_source.hpp"

namespace tgfx
{
    class FontAtlas;
    class RenderContext2;
    class Text3DRenderer;
}

namespace tcplot
{

    class PlotScene3DChartChromeRenderer
    {
    public:
        PlotScene3DChartChromeRenderer();
        ~PlotScene3DChartChromeRenderer();

        PlotScene3DChartChromeRenderer(const PlotScene3DChartChromeRenderer&) =
            delete;
        PlotScene3DChartChromeRenderer&
        operator=(const PlotScene3DChartChromeRenderer&) = delete;

        void draw_grid_labels(tgfx::RenderContext2& context,
                              tgfx::FontAtlas& font,
                              const PlotScene3DFrameRenderState& frame,
                              const tc_grid_item3d_style& style,
                              int viewport_width,
                              int viewport_height);

        void release_gpu();

    private:
        std::unique_ptr<tgfx::Text3DRenderer> text_;
    };

} // namespace tcplot
