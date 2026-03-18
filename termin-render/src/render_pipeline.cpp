// render_pipeline.cpp - Lightweight handle wrapper for tc_pipeline
#include "termin/render/render_pipeline.hpp"

#include "termin/lighting/shadow.hpp"

extern "C" {
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
}

namespace termin {

static void destroy_render_cache(void* ptr) {
    delete static_cast<PipelineRenderCache*>(ptr);
}

RenderPipeline::RenderPipeline(const std::string& name)
    : handle_(tc_pipeline_create(name.c_str())) {}

std::string RenderPipeline::name() const {
    const char* n = tc_pipeline_get_name(handle_);
    return n ? n : "";
}

void RenderPipeline::set_name(const std::string& name) {
    tc_pipeline_set_name(handle_, name.c_str());
}

void RenderPipeline::add_pass(tc_pass* pass) {
    tc_pipeline_add_pass(handle_, pass);
}

void RenderPipeline::remove_pass(tc_pass* pass) {
    tc_pipeline_remove_pass(handle_, pass);
}

size_t RenderPipeline::remove_passes_by_name(const std::string& name) {
    return tc_pipeline_remove_passes_by_name(handle_, name.c_str());
}

void RenderPipeline::insert_pass_before(tc_pass* pass, tc_pass* before) {
    tc_pipeline_insert_pass_before(handle_, pass, before);
}

tc_pass* RenderPipeline::get_pass(const std::string& name) {
    return tc_pipeline_get_pass(handle_, name.c_str());
}

tc_pass* RenderPipeline::get_pass_at(size_t index) {
    return tc_pipeline_get_pass_at(handle_, index);
}

size_t RenderPipeline::pass_count() const {
    return tc_pipeline_pass_count(handle_);
}

// -- Render cache (lazy init in pool) --

PipelineRenderCache& RenderPipeline::cache() {
    void* c = tc_pipeline_get_render_cache(handle_);
    if (!c) {
        c = new PipelineRenderCache();
        tc_pipeline_set_render_cache(handle_, c, destroy_render_cache);
    }
    return *static_cast<PipelineRenderCache*>(c);
}

const FBOPool& RenderPipeline::fbo_pool() const {
    void* c = tc_pipeline_get_render_cache(handle_);
    if (!c) {
        auto* self = const_cast<RenderPipeline*>(this);
        return self->cache().fbo_pool;
    }
    return static_cast<PipelineRenderCache*>(c)->fbo_pool;
}

// -- Specs (in render cache) --

void RenderPipeline::add_spec(const ResourceSpec& spec) {
    cache().specs.push_back(spec);
}

void RenderPipeline::clear_specs() {
    cache().specs.clear();
}

size_t RenderPipeline::spec_count() const {
    void* c = tc_pipeline_get_render_cache(handle_);
    return c ? static_cast<PipelineRenderCache*>(c)->specs.size() : 0;
}

const ResourceSpec* RenderPipeline::get_spec_at(size_t index) const {
    void* c = tc_pipeline_get_render_cache(handle_);
    if (!c) return nullptr;
    auto& specs = static_cast<PipelineRenderCache*>(c)->specs;
    return index < specs.size() ? &specs[index] : nullptr;
}

const std::vector<ResourceSpec>& RenderPipeline::specs() const {
    static const std::vector<ResourceSpec> empty;
    void* c = tc_pipeline_get_render_cache(handle_);
    return c ? static_cast<PipelineRenderCache*>(c)->specs : empty;
}

// -- Dirty --

bool RenderPipeline::is_dirty() const {
    return tc_pipeline_is_dirty(handle_);
}

void RenderPipeline::mark_dirty() {
    tc_pipeline_mark_dirty(handle_);
}

// -- Collect specs --

std::vector<ResourceSpec> RenderPipeline::collect_specs() const {
    std::vector<ResourceSpec> result;
    result.insert(result.end(), specs().begin(), specs().end());

    size_t count = tc_pipeline_pass_count(handle_);
    for (size_t i = 0; i < count; i++) {
        tc_pass* pass = tc_pipeline_get_pass_at(handle_, i);
        if (pass && pass->enabled) {
            ResourceSpec pass_specs[16];
            size_t pass_spec_count = tc_pass_get_resource_specs(pass, pass_specs, 16);
            for (size_t j = 0; j < pass_spec_count; j++) {
                result.push_back(pass_specs[j]);
            }
        }
    }

    return result;
}

// -- Destroy --

void RenderPipeline::destroy() {
    if (tc_pipeline_pool_alive(handle_)) {
        tc_pipeline_destroy(handle_);
    }
    handle_ = TC_PIPELINE_HANDLE_INVALID;
}

} // namespace termin
