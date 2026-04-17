// pipeline_cache.cpp - Lazy pipeline creation from mutable render state.
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/i_render_device.hpp"

#include <functional>

namespace tgfx {

// ============================================================================
// Key equality
// ============================================================================

static bool layouts_equal(const VertexBufferLayout& a, const VertexBufferLayout& b) {
    if (a.stride != b.stride || a.per_instance != b.per_instance)
        return false;
    if (a.attributes.size() != b.attributes.size())
        return false;
    for (size_t i = 0; i < a.attributes.size(); i++) {
        if (a.attributes[i].location != b.attributes[i].location ||
            a.attributes[i].format != b.attributes[i].format ||
            a.attributes[i].offset != b.attributes[i].offset)
            return false;
    }
    return true;
}

bool PipelineCacheKey::operator==(const PipelineCacheKey& o) const {
    return vertex_shader == o.vertex_shader
        && fragment_shader == o.fragment_shader
        && geometry_shader == o.geometry_shader
        && layouts_equal(vertex_layout, o.vertex_layout)
        && topology == o.topology
        && raster.cull == o.raster.cull
        && raster.front_face == o.raster.front_face
        && raster.polygon_mode == o.raster.polygon_mode
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

    hash_combine(h, std::hash<uint32_t>{}(k.vertex_layout.stride));
    hash_combine(h, std::hash<size_t>{}(k.vertex_layout.attributes.size()));
    for (auto& a : k.vertex_layout.attributes) {
        hash_combine(h, std::hash<uint32_t>{}(a.location));
        hash_combine(h, std::hash<int>{}(static_cast<int>(a.format)));
        hash_combine(h, std::hash<uint32_t>{}(a.offset));
    }

    hash_combine(h, std::hash<int>{}(static_cast<int>(k.topology)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.raster.cull)));
    hash_combine(h, std::hash<int>{}(static_cast<int>(k.raster.polygon_mode)));
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
        return it->second;
    }

    // Build PipelineDesc from key
    PipelineDesc desc;
    desc.vertex_shader = key.vertex_shader;
    desc.fragment_shader = key.fragment_shader;
    desc.geometry_shader = key.geometry_shader;

    if (key.vertex_layout.stride > 0) {
        desc.vertex_layouts.push_back(key.vertex_layout);
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
    cache_[key] = handle;
    return handle;
}

void PipelineCache::clear() {
    for (auto& [k, h] : cache_) {
        device_.destroy(h);
    }
    cache_.clear();
}

} // namespace tgfx
