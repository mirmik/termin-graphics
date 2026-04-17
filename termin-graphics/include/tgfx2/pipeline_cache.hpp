// pipeline_cache.hpp - Lazy pipeline creation from mutable render state.
// Bridges the state-machine mental model (set_depth_test, set_blend, etc.)
// to the pipeline-object model (tgfx::PipelineHandle).
#pragma once

#include <cstdint>
#include <unordered_map>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/render_state.hpp"
#include "tgfx2/vertex_layout.hpp"
#include "tgfx2/descriptors.hpp"

namespace tgfx {

class IRenderDevice;

// Key that uniquely identifies a pipeline configuration.
// Hash is computed incrementally for fast lookup.
struct PipelineCacheKey {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    ShaderHandle geometry_shader;

    VertexBufferLayout vertex_layout;

    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    RasterState raster;
    DepthStencilState depth_stencil;
    BlendState blend;
    ColorMask color_mask;

    PixelFormat color_format = PixelFormat::RGBA8_UNorm;
    PixelFormat depth_format = PixelFormat::D32F;
    uint32_t sample_count = 1;

    bool operator==(const PipelineCacheKey& o) const;
};

struct PipelineCacheKeyHash {
    size_t operator()(const PipelineCacheKey& k) const;
};

class TGFX2_API PipelineCache {
public:
    explicit PipelineCache(IRenderDevice& device);
    ~PipelineCache();

    // Get or create a pipeline matching the given key.
    PipelineHandle get(const PipelineCacheKey& key);

    // Clear all cached pipelines (e.g. on device lost).
    void clear();

    // Number of cached pipelines.
    size_t size() const { return cache_.size(); }

private:
    IRenderDevice& device_;
    std::unordered_map<PipelineCacheKey, PipelineHandle, PipelineCacheKeyHash> cache_;
};

} // namespace tgfx
