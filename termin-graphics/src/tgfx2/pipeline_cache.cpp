// pipeline_cache.cpp - Lazy pipeline creation from mutable render state.
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/i_render_device.hpp"

#ifdef TGFX2_HAS_VULKAN
#include "vulkan/vulkan_stats.hpp"
#endif

#include <algorithm>
#include <functional>

namespace tgfx {

// ============================================================================
// Key equality
// ============================================================================

static bool layouts_equal(const VertexBufferLayout& a, const VertexBufferLayout& b) {
    if (a.stride != b.stride || a.per_instance != b.per_instance ||
        a.use_shader_input_locations != b.use_shader_input_locations)
        return false;
    if (a.attributes.size() != b.attributes.size())
        return false;
    for (size_t i = 0; i < a.attributes.size(); i++) {
        if (a.attributes[i].format != b.attributes[i].format ||
            a.attributes[i].offset != b.attributes[i].offset ||
            a.attributes[i].semantic != b.attributes[i].semantic)
            return false;
        if (!a.use_shader_input_locations &&
            a.attributes[i].location != b.attributes[i].location)
            return false;
    }
    return true;
}

static bool layout_vectors_equal(const std::vector<VertexBufferLayout>& a,
                                 const std::vector<VertexBufferLayout>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (!layouts_equal(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

bool PipelineCacheKey::operator==(const PipelineCacheKey& o) const {
    return vertex_shader == o.vertex_shader
        && fragment_shader == o.fragment_shader
        && geometry_shader == o.geometry_shader
        && layout_vectors_equal(vertex_layouts, o.vertex_layouts)
        && topology == o.topology
        && raster.cull == o.raster.cull
        && raster.front_face == o.raster.front_face
        && raster.polygon_mode == o.raster.polygon_mode
        && raster.depth_bias_enabled == o.raster.depth_bias_enabled
        && raster.depth_bias_constant == o.raster.depth_bias_constant
        && raster.depth_bias_slope == o.raster.depth_bias_slope
        && raster.depth_bias_clamp == o.raster.depth_bias_clamp
        && depth_stencil.depth_test == o.depth_stencil.depth_test
        && depth_stencil.depth_write == o.depth_stencil.depth_write
        && depth_stencil.depth_compare == o.depth_stencil.depth_compare
        && blend.enabled == o.blend.enabled
        && blend.src_color == o.blend.src_color
        && blend.dst_color == o.blend.dst_color
        && blend.color_op == o.blend.color_op
        && blend.src_alpha == o.blend.src_alpha
        && blend.dst_alpha == o.blend.dst_alpha
        && blend.alpha_op == o.blend.alpha_op
        && color_mask.r == o.color_mask.r
        && color_mask.g == o.color_mask.g
        && color_mask.b == o.color_mask.b
        && color_mask.a == o.color_mask.a
        && color_format == o.color_format
        && depth_format == o.depth_format
        && sample_count == o.sample_count;
}

// ============================================================================
// Key hash
// ============================================================================

static void hash_combine(size_t& seed, size_t v) {
    seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

size_t PipelineCacheKeyHash::operator()(const PipelineCacheKey& k) const {
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

// ============================================================================
// PipelineCache
// ============================================================================

PipelineCache::PipelineCache(IRenderDevice& device) : device_(device) {}

PipelineCache::~PipelineCache() {
    clear();
}

PipelineHandle PipelineCache::get(const PipelineCacheKey& key) {
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

    for (const VertexBufferLayout& layout : key.vertex_layouts) {
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
    cache_[key] = handle;
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
