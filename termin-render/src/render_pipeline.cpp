// render_pipeline.cpp - Lightweight handle wrapper for tc_pipeline
#include "termin/render/render_pipeline.hpp"

#include <tcbase/tc_log.hpp>
#include <stdexcept>
#include <trent/json.h>

#include "termin/lighting/shadow.hpp"
#include "termin/render/frame_pass.hpp"
#include "termin/render/builtin_passes.hpp"
#include "termin/render/tc_pass.hpp"
#include "termin/render/unknown_pass.hpp"
#include "tc_inspect_cpp.hpp"
#include "tc_value_trent.hpp"

extern "C" {
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
#include "render/tc_pipeline_template_registry.h"
}

namespace termin {

static void destroy_render_cache(void* ptr) {
    delete static_cast<PipelineRenderCache*>(ptr);
}

RenderPipeline::RenderPipeline(const std::string& name)
    : handle_(tc_pipeline_create(name.c_str())) {}

RenderPipeline::RenderPipeline(const TcPipelineTemplate& pipeline_template)
    : handle_(tc_pipeline_create_from_template(pipeline_template.handle)) {
    const tc_pipeline_template* definition = pipeline_template.get();
    if (!definition || !is_valid()) {
        throw std::runtime_error("cannot instantiate an invalid pipeline template");
    }

    try {
        tc::init_cpp_inspect_vtable();
        // tc_pipeline_adopt_pass prepends, so walk backwards to preserve the
        // canonical descriptor order.
        for (uint32_t offset = 0; offset < definition->pass_count; ++offset) {
            const uint32_t index = definition->pass_count - offset - 1;
            const tc_pipeline_template_pass_desc& desc = definition->passes[index];
            tc_pass* pass = tc_pass_registry_create(desc.type_name);
            if (!pass) {
                if (!tc_pass_registry_has("UnknownPass")) register_builtin_render_pass_types();
                pass = tc_pass_registry_create("UnknownPass");
                auto* unknown = pass
                    ? dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(pass))
                    : nullptr;
                if (!unknown) {
                    throw std::runtime_error(
                        std::string("cannot instantiate pass type '") + desc.type_name + "'");
                }
                unknown->original_type = desc.type_name;
            }

            TcPassRef pass_ref(pass);
            pass_ref.set_pass_name(desc.name);
            if (desc.viewport_name) pass_ref.set_viewport_name(desc.viewport_name);
            if (desc.parameters && desc.parameters[0]) {
                const nos::trent parameters = nos::json::parse(desc.parameters);
                if (!parameters.is_dict()) {
                    tc_pass_delete_unowned(pass);
                    throw std::runtime_error(
                        std::string("parameters for pass '") + desc.name + "' are not an object");
                }
                if (auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(pass))) {
                    tc_value_free(&unknown->original_data);
                    unknown->original_data = tc::trent_to_tc_value(parameters);
                } else {
                    for (const auto& [field_name, value] : parameters.as_dict()) {
                        tc_value field_value = tc::trent_to_tc_value(value);
                        const bool assigned = pass_ref.set_field(field_name, field_value);
                        tc_value_free(&field_value);
                        if (!assigned) {
                            tc::Log::warn(
                                "RenderPipeline: failed to restore field '%s.%s'",
                                desc.type_name,
                                field_name.c_str());
                        }
                    }
                }
            }
            if (!tc_pipeline_adopt_pass(handle_, pass, pass->deleter)) {
                tc_pass_delete_unowned(pass);
                throw std::runtime_error(
                    std::string("cannot adopt pass '") + desc.name + "'");
            }
        }

        for (uint32_t i = 0; i < definition->resource_count; ++i) {
            const tc_pipeline_template_resource_desc& desc = definition->resources[i];
            const std::string resource_type = desc.resource_type;
            if (resource_type.starts_with("external")) continue;
            ResourceSpec spec;
            spec.resource = desc.name;
            spec.resource_type = resource_type;
            if (desc.format) spec.format = desc.format;
            if (desc.viewport_name) spec.viewport_name = desc.viewport_name;
            if (desc.width > 0 && desc.height > 0) spec.size = {desc.width, desc.height};
            spec.scale = desc.scale;
            spec.samples = static_cast<int>(desc.samples);
            add_spec(spec);
        }

        PipelineRenderCache& render_cache = cache();
        for (uint32_t i = 0; i < definition->resource_view_count; ++i) {
            const tc_pipeline_template_resource_view_desc& desc =
                definition->resource_views[i];
            render_cache.resource_views.emplace(
                desc.name,
                ResourceView{
                    desc.parent,
                    desc.attachment == TC_PIPELINE_ATTACHMENT_DEPTH
                        ? AttachmentKind::Depth
                        : AttachmentKind::Color,
                });
        }
        for (uint32_t i = 0; i < definition->fbo_composition_count; ++i) {
            const tc_pipeline_template_fbo_composition_desc& desc =
                definition->fbo_compositions[i];
            render_cache.fbo_compositions.emplace(
                desc.name,
                FboComposition{
                    desc.color ? desc.color : "",
                    desc.depth ? desc.depth : "",
                });
        }
    } catch (...) {
        destroy();
        throw;
    }
}

std::string RenderPipeline::name() const {
    const char* n = tc_pipeline_get_name(handle_);
    return n ? n : "";
}

void RenderPipeline::set_name(const std::string& name) {
    tc_pipeline_set_name(handle_, name.c_str());
}

void RenderPipeline::add_pass(tc_pass* pass) {
    if (pass) {
        tc_pipeline_adopt_pass(handle_, pass, pass->deleter);
    }
}

void RenderPipeline::remove_pass(tc_pass* pass) {
    tc_pipeline_remove_pass(handle_, pass);
}

size_t RenderPipeline::remove_passes_by_name(const std::string& name) {
    return tc_pipeline_remove_passes_by_name(handle_, name.c_str());
}

void RenderPipeline::insert_pass_before(tc_pass* pass, tc_pass* before) {
    if (pass) {
        tc_pipeline_adopt_pass_before(handle_, pass, pass->deleter, before);
    }
}

bool RenderPipeline::move_pass_before(tc_pass* pass, tc_pass* before) {
    return tc_pipeline_move_pass_before(handle_, pass, before);
}

tc_pass* RenderPipeline::get_pass(const std::string& name) {
    return tc_pipeline_get_pass(handle_, name.c_str());
}

tc_pass* RenderPipeline::get_pass_at(size_t index) {
    return tc_pipeline_get_pass_at(handle_, index);
}

const tc_pass* RenderPipeline::get_pass_at(size_t index) const {
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
            const size_t pass_spec_count =
                tc_pass_get_resource_specs(pass, nullptr, 0);
            std::vector<ResourceSpec> pass_specs(pass_spec_count);
            const size_t actual = tc_pass_get_resource_specs(
                pass, pass_specs.data(), pass_specs.size());
            if (actual != pass_spec_count) {
                tc::Log::error(
                    "RenderPipeline::collect_specs: pass '%s' changed its resource "
                    "spec count during collection (%zu -> %zu)",
                    pass->pass_name ? pass->pass_name : "<unnamed>",
                    pass_spec_count,
                    actual
                );
                continue;
            }
            result.insert(result.end(), pass_specs.begin(), pass_specs.end());
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
