#pragma once

#include "tcplot/plot_scene3d_render_item_source.hpp"

namespace tcplot
{

    void
    build_plot_scene3d_surface_draw_stream(PlotScene3DItemRenderData& data);

    void ensure_plot_scene3d_surface_encoder_registered();

} // namespace tcplot
