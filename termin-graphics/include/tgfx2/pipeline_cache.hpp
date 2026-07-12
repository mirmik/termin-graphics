// pipeline_cache.hpp - Lazy pipeline creation from mutable render state.
// Bridges the state-machine mental model (set_depth_test, set_blend, etc.)
// to the pipeline-object model (tgfx::PipelineHandle).
#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/render_state.hpp"
#include "tgfx2/vertex_layout.hpp"
#include "tgfx2/descriptors.hpp"

namespace tgfx {

class IRenderDevice;

// State shared by a transient lookup view and a cache-owned key.  Vertex
// layouts deliberately live outside this structure: callers borrow them for
// lookup, while PipelineCache owns them after a miss.
struct PipelineCacheKeyState {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    ShaderHandle geometry_shader;

    // Precomputed hash of `vertex_layouts` filled by the caller
    // (RenderContext2 caches it on set_vertex_layout* and writes it here
    // on every flush_pipeline). Lets PipelineCacheKeyHash skip iterating
    // vertex attributes on every lookup.
    size_t vertex_layouts_hash = 0;

    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    RasterState raster;
    DepthStencilState depth_stencil;
    BlendState blend;
    ColorMask color_mask;

    PixelFormat color_format = PixelFormat::RGBA8_UNorm;
    PixelFormat depth_format = PixelFormat::D32F;
    uint32_t sample_count = 1;
};

// Non-owning pipeline-cache request. Its vertex-layout view need only remain
// valid for the get() call; cache misses copy it into PipelineCacheKey.
struct PipelineCacheLookupKey : PipelineCacheKeyState {
    std::span<const VertexLayoutDesc> vertex_layouts;
};

// Canonical cache-owned pipeline identity. It is never constructed on a cache
// hit, so RenderContext2's pending vertex-layout vector is not copied per draw.
struct PipelineCacheKey : PipelineCacheKeyState {
    std::vector<VertexLayoutDesc> vertex_layouts;

    PipelineCacheKey() = default;
    explicit PipelineCacheKey(const PipelineCacheLookupKey& lookup);
};

struct PipelineCacheKeyHash {
    using is_transparent = void;

    size_t operator()(const PipelineCacheKey& k) const;
    size_t operator()(const PipelineCacheLookupKey& k) const;
};

struct PipelineCacheKeyEqual {
    using is_transparent = void;

    bool operator()(const PipelineCacheKey& a, const PipelineCacheKey& b) const;
    bool operator()(const PipelineCacheKey& a, const PipelineCacheLookupKey& b) const;
    bool operator()(const PipelineCacheLookupKey& a, const PipelineCacheKey& b) const;
};

struct PipelineCacheStats {
    uint64_t hit_count = 0;
    uint64_t miss_count = 0;
    uint64_t create_pipeline_count = 0;
    uint64_t unique_vertex_layout_signature_count = 0;
    size_t cached_pipeline_count = 0;
    std::vector<size_t> vertex_layout_signature_hashes;
};

class TGFX2_TYPE_API PipelineCache {
private:
    IRenderDevice& device_;
    std::unordered_map<
        PipelineCacheKey,
        PipelineHandle,
        PipelineCacheKeyHash,
        PipelineCacheKeyEqual
    > cache_;
    std::unordered_set<size_t> observed_vertex_layout_hashes_;
    uint64_t hit_count_ = 0;
    uint64_t miss_count_ = 0;
    uint64_t create_pipeline_count_ = 0;

public:
    explicit PipelineCache(IRenderDevice& device);
    ~PipelineCache();

    // Get or create a pipeline matching the given key.
    PipelineHandle get(const PipelineCacheLookupKey& key);

    // Clear all cached pipelines (e.g. on device lost).
    void clear();

    PipelineCacheStats stats() const;

    // Number of cached pipelines.
    size_t size() const { return cache_.size(); }
};

} // namespace tgfx
