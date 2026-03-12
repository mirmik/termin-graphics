// render_pipeline.cpp - C++ RenderPipeline implementation
#include "termin/render/render_pipeline.hpp"

#include "termin/lighting/shadow.hpp"

extern "C" {
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
}

namespace termin {

RenderPipeline::RenderPipeline(const std::string& name)
    : handle_(TC_PIPELINE_HANDLE_INVALID),
      name_(name) {
    handle_ = tc_pipeline_create(name.c_str());
    if (tc_pipeline_pool_alive(handle_)) {
        tc_pipeline_set_cpp_owner(handle_, this);
    }
}

RenderPipeline::~RenderPipeline() {
    if (tc_pipeline_pool_alive(handle_)) {
        tc_pipeline_set_cpp_owner(handle_, nullptr);
        tc_pipeline_destroy(handle_);
    }
    handle_ = TC_PIPELINE_HANDLE_INVALID;
}

RenderPipeline::RenderPipeline(RenderPipeline&& other) noexcept
    : handle_(other.handle_),
      specs_(std::move(other.specs_)),
      name_(std::move(other.name_)),
      fbo_pool_(std::move(other.fbo_pool_)),
      shadow_arrays_(std::move(other.shadow_arrays_)) {
    if (tc_pipeline_pool_alive(handle_)) {
        tc_pipeline_set_cpp_owner(handle_, this);
    }

    other.handle_ = TC_PIPELINE_HANDLE_INVALID;
}

RenderPipeline& RenderPipeline::operator=(RenderPipeline&& other) noexcept {
    if (this != &other) {
        if (tc_pipeline_pool_alive(handle_)) {
            tc_pipeline_set_cpp_owner(handle_, nullptr);
            tc_pipeline_destroy(handle_);
        }

        handle_ = other.handle_;
        specs_ = std::move(other.specs_);
        name_ = std::move(other.name_);
        fbo_pool_ = std::move(other.fbo_pool_);
        shadow_arrays_ = std::move(other.shadow_arrays_);

        if (tc_pipeline_pool_alive(handle_)) {
            tc_pipeline_set_cpp_owner(handle_, this);
        }

        other.handle_ = TC_PIPELINE_HANDLE_INVALID;
    }
    return *this;
}

void RenderPipeline::set_name(const std::string& name) {
    name_ = name;
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

void RenderPipeline::add_spec(const ResourceSpec& spec) {
    specs_.push_back(spec);
}

void RenderPipeline::clear_specs() {
    specs_.clear();
}

const ResourceSpec* RenderPipeline::get_spec_at(size_t index) const {
    if (index >= specs_.size()) {
        return nullptr;
    }
    return &specs_[index];
}

bool RenderPipeline::is_dirty() const {
    return tc_pipeline_is_dirty(handle_);
}

void RenderPipeline::mark_dirty() {
    tc_pipeline_mark_dirty(handle_);
}

std::vector<ResourceSpec> RenderPipeline::collect_specs() const {
    std::vector<ResourceSpec> result;
    result.insert(result.end(), specs_.begin(), specs_.end());

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

RenderPipeline* RenderPipeline::from_handle(tc_pipeline_handle h) {
    void* owner = tc_pipeline_get_cpp_owner(h);
    if (!owner) {
        return nullptr;
    }
    return static_cast<RenderPipeline*>(owner);
}

} // namespace termin
