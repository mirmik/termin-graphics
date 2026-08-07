#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <tgfx2/handles.hpp>

namespace termin
{
    class RenderItemSnapshot;
}

namespace tcplot
{

    class GpuHost;

    struct PlotScene3DRenderedItem
    {
        std::uint64_t object_id = 0;
        std::uint32_t generation = 0;
    };

    struct PlotScene3DRenderResult
    {
        bool success = false;
        std::vector<PlotScene3DRenderedItem> rendered_items;
    };

    class PlotScene3DRenderPipeline
    {
    public:
        explicit PlotScene3DRenderPipeline(GpuHost& host);
        ~PlotScene3DRenderPipeline();

        PlotScene3DRenderPipeline(const PlotScene3DRenderPipeline&) = delete;
        PlotScene3DRenderPipeline&
        operator=(const PlotScene3DRenderPipeline&) = delete;

        PlotScene3DRenderResult
        execute(const termin::RenderItemSnapshot& snapshot,
                std::uint64_t selected_grid_namespace,
                std::uint64_t selected_grid_object,
                std::uint32_t selected_grid_generation,
                tgfx::TextureHandle color,
                tgfx::TextureHandle depth,
                int width,
                int height);

        void release_gpu();

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace tcplot
