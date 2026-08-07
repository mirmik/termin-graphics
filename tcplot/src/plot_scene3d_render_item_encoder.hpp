#pragma once

#include "tcplot/plot_scene3d_render_item_source.hpp"

namespace tcplot
{

    void
    build_plot_scene3d_surface_draw_stream(PlotScene3DItemRenderData& data);

    void
    build_plot_scene3d_scatter_draw_stream(PlotScene3DItemRenderData& data);

    void ensure_plot_scene3d_render_item_encoders_registered();

} // namespace tcplot
