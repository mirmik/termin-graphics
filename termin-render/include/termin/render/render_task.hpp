#pragma once

#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <termin/entity/component.hpp>
#include <termin/render/render_context.hpp>
#include <termin/render/render_item_submission.hpp>

namespace termin {

class RenderTaskExtension;

// Pass-neutral part of a planned render submission.  It deliberately carries
// no RenderItem-kind fields: mesh, line, text and foliage tasks use the same
// lifetime-safe item/context/resource packet.
struct RENDER_API RenderTask {
    size_t source_draw_index = SIZE_MAX;
    size_t item_index = SIZE_MAX;
    const tc_render_item* item = nullptr;
    Entity entity;
    tc_component* component = nullptr;
    tc_material_phase* material_phase = nullptr;
    tc_shader_handle final_shader = tc_shader_handle_invalid();
    std::string entity_name;
    RenderContext draw_context;
    RenderItemResourceBinding resources{};
    RenderTaskExtension* extension = nullptr;

private:
    std::vector<RenderItemNamedUniformBinding> named_uniforms_;
    std::vector<RenderItemNamedTextureBinding> named_textures_;

public:
    void set_resources(
        const MaterialPipelineResourceView* material_resources,
        std::span<const RenderItemNamedUniformBinding> named_uniforms,
        std::span<const RenderItemNamedTextureBinding> named_textures = {})
    {
        named_uniforms_.clear();
        named_uniforms_.reserve(named_uniforms.size());
        for (const RenderItemNamedUniformBinding& binding : named_uniforms) {
            named_uniforms_.push_back(binding);
        }
        named_textures_.clear();
        named_textures_.reserve(named_textures.size());
        for (const RenderItemNamedTextureBinding& binding : named_textures) {
            named_textures_.push_back(binding);
        }
        resources = {};
        resources.material_resources = material_resources;
        resources.named_uniforms = named_uniforms_.data();
        resources.named_uniform_count = static_cast<uint32_t>(named_uniforms_.size());
        resources.named_textures = named_textures_.data();
        resources.named_texture_count = static_cast<uint32_t>(named_textures_.size());
    }
};

// Pass-specific, typed UBO payloads are owned here on the heap so pointers in
// RenderTask resource packets cannot be invalidated when task vectors grow.
class RENDER_API RenderTaskExtension {
public:
    virtual ~RenderTaskExtension() = default;
};

class RENDER_API RenderTaskList {
    std::vector<RenderTask> tasks_;
    std::vector<std::unique_ptr<RenderTaskExtension>> extensions_;

public:
    void reserve(size_t count) { tasks_.reserve(count); }
    RenderTask& append() {
        tasks_.emplace_back();
        return tasks_.back();
    }

    template <typename T, typename... Args>
    T& emplace_extension(Args&&... args) {
        static_assert(std::is_base_of_v<RenderTaskExtension, T>);
        auto extension = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *extension;
        extensions_.push_back(std::move(extension));
        return result;
    }

    bool empty() const { return tasks_.empty(); }
    size_t size() const { return tasks_.size(); }
    std::vector<RenderTask>::iterator begin() { return tasks_.begin(); }
    std::vector<RenderTask>::iterator end() { return tasks_.end(); }
    std::vector<RenderTask>::const_iterator begin() const { return tasks_.begin(); }
    std::vector<RenderTask>::const_iterator end() const { return tasks_.end(); }
};

} // namespace termin
