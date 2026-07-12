// pipeline_cache.cpp - Lazy pipeline creation from mutable render state.
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/i_render_device.hpp"

#ifdef TGFX2_HAS_VULKAN
#include "vulkan/vulkan_stats.hpp"
#endif

#include <algorithm>
#include <functional>

#include <tcbase/tc_log.h>

namespace tgfx {

// ============================================================================
// Key equality
// ============================================================================

static bool layout_ranges_equal(std::span<const VertexLayoutDesc> a,
                                std::span<const VertexLayoutDesc> b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (!vertex_layout_desc_equal(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

static bool pipeline_cache_key_state_equal(const PipelineCacheKeyState& a,
                                           const PipelineCacheKeyState& b) {
    return a.vertex_shader == b.vertex_shader
        && a.fragment_shader == b.fragment_shader
        && a.geometry_shader == b.geometry_shader
        && a.topology == b.topology
        && a.raster.cull == b.raster.cull
        && a.raster.front_face == b.raster.front_face
        && a.raster.polygon_mode == b.raster.polygon_mode
        && a.raster.depth_bias_enabled == b.raster.depth_bias_enabled
        && a.raster.depth_bias_constant == b.raster.depth_bias_constant
        && a.raster.depth_bias_slope == b.raster.depth_bias_slope
        && a.raster.depth_bias_clamp == b.raster.depth_bias_clamp
        && a.depth_stencil.depth_test == b.depth_stencil.depth_test
        && a.depth_stencil.depth_write == b.depth_stencil.depth_write
        && a.depth_stencil.depth_compare == b.depth_stencil.depth_compare
        && a.blend.enabled == b.blend.enabled
        && a.blend.src_color == b.blend.src_color
        && a.blend.dst_color == b.blend.dst_color
        && a.blend.color_op == b.blend.color_op
        && a.blend.src_alpha == b.blend.src_alpha
        && a.blend.dst_alpha == b.blend.dst_alpha
        && a.blend.alpha_op == b.blend.alpha_op
        && a.color_mask.r == b.color_mask.r
        && a.color_mask.g == b.color_mask.g
        && a.color_mask.b == b.color_mask.b
        && a.color_mask.a == b.color_mask.a
        && a.color_format == b.color_format
        && a.depth_format == b.depth_format
        && a.sample_count == b.sample_count;
}

PipelineCacheKey::PipelineCacheKey(const PipelineCacheLookupKey& lookup)
    : PipelineCacheKeyState(lookup)
    , vertex_layouts(lookup.vertex_layouts.begin(), lookup.vertex_layouts.end()) {}

bool PipelineCacheKeyEqual::operator()(const PipelineCacheKey& a,
                                       const PipelineCacheKey& b) const {
    return pipeline_cache_key_state_equal(a, b) &&
        layout_ranges_equal(a.vertex_layouts, b.vertex_layouts);
}

bool PipelineCacheKeyEqual::operator()(const PipelineCacheKey& a,
                                       const PipelineCacheLookupKey& b) const {
    return pipeline_cache_key_state_equal(a, b) &&
        layout_ranges_equal(a.vertex_layouts, b.vertex_layouts);
}

bool PipelineCacheKeyEqual::operator()(const PipelineCacheLookupKey& a,
                                       const PipelineCacheKey& b) const {
    return (*this)(b, a);
}

// ============================================================================
// Key hash
// ============================================================================

static void hash_combine(size_t& seed, size_t v) {
    seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

static size_t pipeline_cache_key_hash(const PipelineCacheKeyState& k) {
    size_t h = 0;
    hash_combine(h, std::hash<uint32_t>{}(k.vertex_shader.id));
    hash_combine(h, std::hash<uint32_t>{}(k.fragment_shader.id));
    hash_combine(h, std::hash<uint32_t>{}(k.geometry_shader.id));

    // Use the precomputed layout hash — the caller is responsible for
    // keeping it in sync with `vertex_layouts`. Skips an O(attributes)
    // loop on every cache lookup (see PipelineCacheKey::vertex_layouts_hash).
    hash_combine(h, k.vertex_layouts_hash);

    hash_combine(h, std::hash<int>{}(static_cast<int>(k.topology)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.raster.cull)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.raster.front_face)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.raster.polygon_mode)));
    hash_combine(h, std::hash<bool>{}(k.raster.depth_bias_enabled));
    hash_combine(h, std::hash<float>{}(k.raster.depth_bias_constant));
    hash_combine(h, std::hash<float>{}(k.raster.depth_bias_slope));
    hash_combine(h, std::hash<float>{}(k.raster.depth_bias_clamp));
    hash_combine(h, std::hash<bool>{}(k.depth_stencil.depth_test));
    hash_combine(h, std::hash<bool>{}(k.depth_stencil.depth_write));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.depth_stencil.depth_compare)));
    hash_combine(h, std::hash<bool>{}(k.blend.enabled));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.blend.src_color)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.blend.dst_color)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.color_format)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.depth_format)));
    hash_combine(h, std::hash<uint32_t>{}(k.sample_count));

    return h;
}

size_t PipelineCacheKeyHash::operator()(const PipelineCacheKey& k) const {
    return pipeline_cache_key_hash(k);
}

size_t PipelineCacheKeyHash::operator()(const PipelineCacheLookupKey& k) const {
    return pipeline_cache_key_hash(k);
}

// ============================================================================
// PipelineCache
// ============================================================================

PipelineCache::PipelineCache(IRenderDevice& device) : device_(device) {}

PipelineCache::~PipelineCache() {
    clear();
}

PipelineHandle PipelineCache::get(const PipelineCacheLookupKey& key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        ++hit_count_;
#ifdef TGFX2_HAS_VULKAN
        g_pipeline_cache_hit_count.fetch_add(1, std::memory_order_relaxed);
#endif
        return it->second;
    }

    ++miss_count_;
    const bool new_vertex_layout_signature =
        observed_vertex_layout_hashes_.insert(key.vertex_layouts_hash).second;
#ifdef TGFX2_HAS_VULKAN
    g_pipeline_cache_miss_count.fetch_add(1, std::memory_order_relaxed);
    if (new_vertex_layout_signature) {
        g_pipeline_cache_unique_vertex_layout_count.fetch_add(1, std::memory_order_relaxed);
    }
#endif

    // Build PipelineDesc from key
    PipelineDesc desc;
    desc.vertex_shader = key.vertex_shader;
    desc.fragment_shader = key.fragment_shader;
    desc.geometry_shader = key.geometry_shader;

    for (const VertexLayoutDesc& layout : key.vertex_layouts) {
        if (layout.stride > 0) {
            desc.vertex_layouts.push_back(layout);
        }
    }

    desc.topology = key.topology;
    desc.raster = key.raster;
    desc.depth_stencil = key.depth_stencil;
    desc.blend = key.blend;
    desc.color_mask = key.color_mask;
    desc.color_formats = {key.color_format};
    desc.depth_format = key.depth_format;
    desc.sample_count = key.sample_count;

    auto handle = device_.create_pipeline(desc);
    ++create_pipeline_count_;
    if (!handle) {
        tc_log(TC_LOG_ERROR,
               "PipelineCache: backend failed to create a graphics pipeline; request will be retried");
        return {};
    }
    cache_.emplace(PipelineCacheKey(key), handle);
    return handle;
}

void PipelineCache::clear() {
    for (auto& [k, h] : cache_) {
        device_.destroy(h);
    }
    cache_.clear();
    observed_vertex_layout_hashes_.clear();
}

PipelineCacheStats PipelineCache::stats() const {
    PipelineCacheStats out;
    out.hit_count = hit_count_;
    out.miss_count = miss_count_;
    out.create_pipeline_count = create_pipeline_count_;
    out.cached_pipeline_count = cache_.size();
    out.unique_vertex_layout_signature_count =
        static_cast<uint64_t>(observed_vertex_layout_hashes_.size());
    out.vertex_layout_signature_hashes.assign(
        observed_vertex_layout_hashes_.begin(),
        observed_vertex_layout_hashes_.end());
    std::sort(out.vertex_layout_signature_hashes.begin(),
              out.vertex_layout_signature_hashes.end());
    return out;
}

} // namespace tgfx
