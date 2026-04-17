// render_pipeline.hpp - Lightweight handle wrapper for tc_pipeline
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/render/render_export.hpp>
#include "termin/render/fbo_pool.hpp"
#include "termin/render/resource_spec.hpp"

extern "C" {
#include "render/tc_pipeline.h"
}

// Full definition needed for unique_ptr member in PipelineRenderCache
#include "termin/lighting/shadow.hpp"

namespace termin {

// Opaque render cache stored in tc_pipeline.render_cache
struct RENDER_API PipelineRenderCache {
    FBOPool fbo_pool;
    std::unordered_map<std::string, std::unique_ptr<ShadowMapArrayResource>> shadow_arrays;
    std::vector<ResourceSpec> specs;
};

// Lightweight handle wrapper. Does NOT own the pipeline.
// Pipeline lifetime is managed by tc_pipeline_create / tc_pipeline_destroy.
class RENDER_API RenderPipeline {
public:
    tc_pipeline_handle handle_;

public:
    RenderPipeline() : handle_(TC_PIPELINE_HANDLE_INVALID) {}
    explicit RenderPipeline(tc_pipeline_handle h) : handle_(h) {}

    // Create a new pipeline in the pool (caller must destroy() when done)
    explicit RenderPipeline(const std::string& name);

    tc_pipeline* ptr() { return tc_pipeline_get_ptr(handle_); }
    const tc_pipeline* ptr() const { return tc_pipeline_get_ptr(handle_); }

    tc_pipeline_handle handle() const { return handle_; }
    bool is_valid() const { return tc_pipeline_pool_alive(handle_); }

    std::string name() const;
    void set_name(const std::string& name);

    void add_pass(tc_pass* pass);
    void remove_pass(tc_pass* pass);
    size_t remove_passes_by_name(const std::string& name);
    void insert_pass_before(tc_pass* pass, tc_pass* before);
    tc_pass* get_pass(const std::string& name);
    tc_pass* get_pass_at(size_t index);
    size_t pass_count() const;

    void add_spec(const ResourceSpec& spec);
    void clear_specs();
    size_t spec_count() const;
    const ResourceSpec* get_spec_at(size_t index) const;
    const std::vector<ResourceSpec>& specs() const;

    bool is_dirty() const;
    void mark_dirty();

    std::vector<ResourceSpec> collect_specs() const;

    // Render cache (lazy-created in pool)
    PipelineRenderCache& cache();
    FBOPool& fbo_pool() { return cache().fbo_pool; }
    const FBOPool& fbo_pool() const;

    tgfx::TextureHandle get_color_tex2(const std::string& name) {
        return fbo_pool().get_color_tgfx2(name);
    }
    tgfx::TextureHandle get_depth_tex2(const std::string& name) {
        return fbo_pool().get_depth_tgfx2(name);
    }
    tgfx::IRenderDevice* tex2_device() {
        return fbo_pool().device();
    }

    std::unordered_map<std::string, std::unique_ptr<ShadowMapArrayResource>>& shadow_arrays() {
        return cache().shadow_arrays;
    }

    // Destroy pipeline in pool (frees passes, render_cache, frame_graph)
    void destroy();
};

} // namespace termin
