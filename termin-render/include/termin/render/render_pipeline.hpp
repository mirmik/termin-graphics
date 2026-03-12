// render_pipeline.hpp - C++ wrapper for tc_pipeline with specs storage
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "termin/render/fbo_pool.hpp"
#include "termin/render/resource_spec.hpp"

extern "C" {
#include "render/tc_pipeline.h"
}

namespace termin {

class ShadowMapArrayResource;

class RenderPipeline {
public:
    tc_pipeline_handle handle_;
    std::vector<ResourceSpec> specs_;
    std::string name_;
    FBOPool fbo_pool_;
    std::unordered_map<std::string, std::unique_ptr<ShadowMapArrayResource>> shadow_arrays_;

public:
    RenderPipeline(const std::string& name = "default");
    ~RenderPipeline();

    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;

    RenderPipeline(RenderPipeline&& other) noexcept;
    RenderPipeline& operator=(RenderPipeline&& other) noexcept;

    tc_pipeline* ptr() { return tc_pipeline_get_ptr(handle_); }
    const tc_pipeline* ptr() const { return tc_pipeline_get_ptr(handle_); }

    tc_pipeline_handle handle() const { return handle_; }

    bool is_valid() const { return tc_pipeline_pool_alive(handle_); }

    const std::string& name() const { return name_; }
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
    size_t spec_count() const { return specs_.size(); }
    const ResourceSpec* get_spec_at(size_t index) const;
    const std::vector<ResourceSpec>& specs() const { return specs_; }

    bool is_dirty() const;
    void mark_dirty();

    std::vector<ResourceSpec> collect_specs() const;

    FBOPool& fbo_pool() { return fbo_pool_; }
    const FBOPool& fbo_pool() const { return fbo_pool_; }

    FramebufferHandle* get_fbo(const std::string& name) {
        return fbo_pool_.get(name);
    }

    std::unordered_map<std::string, std::unique_ptr<ShadowMapArrayResource>>& shadow_arrays() {
        return shadow_arrays_;
    }

    static RenderPipeline* from_handle(tc_pipeline_handle h);
};

} // namespace termin
