#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <tgfx2/handles.hpp>

#include "tcplot/retained_chart3d.h"

namespace termin {
    class RenderItemSnapshot;
}

namespace tcplot {

    class GpuHost;

    struct PlotScene3DRenderedItem {
        std::uint64_t object_id = 0;
        std::uint32_t generation = 0;
    };

    struct PlotScene3DRenderResult {
        bool success = false;
        std::vector<PlotScene3DRenderedItem> rendered_items;
    };

    class PlotScene3DRenderPipeline {
    public:
        PlotScene3DRenderPipeline(GpuHost& host, int sample_count);
        ~PlotScene3DRenderPipeline();

        PlotScene3DRenderPipeline(const PlotScene3DRenderPipeline&) = delete;
        PlotScene3DRenderPipeline& operator=(const PlotScene3DRenderPipeline&) = delete;

        PlotScene3DRenderResult execute(const termin::RenderItemSnapshot& snapshot,
                                        std::uint64_t selected_grid_namespace,
                                        std::uint64_t selected_grid_object,
                                        std::uint32_t selected_grid_generation,
                                        std::uint64_t colorbar_surface_object,
                                        std::uint32_t colorbar_surface_generation,
                                        const std::string& colorbar_label,
                                        const tc_colorbar3d_style& colorbar_style,
                                        tgfx::TextureHandle color,
                                        int width,
                                        int height);

        void release_gpu();

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace tcplot
