#pragma once

#include <array>
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
    std::array<tc_shader_handle, RenderItemTaskShaderPlan::MAX_SHADER_USAGES>
        shader_usages{};
    uint32_t shader_usage_count = 0;
    RenderItemPassSemantic pass_semantic = RenderItemPassSemantic::Color;
    VertexTransformKind vertex_transform_kind = VertexTransformKind::StaticMesh;
    bool has_vertex_transform_kind = false;
    std::string entity_name;
    RenderContext draw_context;
    RenderItemResourceBinding resources{};
    RenderTaskExtension* extension = nullptr;

private:
    std::vector<RenderItemNamedUniformBinding> named_uniforms_;
    std::vector<RenderItemNamedTextureBinding> named_textures_;

public:
    void set_resources(const MaterialPipelineResourceView* material_resources,
                       std::span<const RenderItemNamedUniformBinding> named_uniforms,
                       std::span<const RenderItemNamedTextureBinding> named_textures = {});
};

// Pass-specific, typed UBO payloads are owned here on the heap so pointers in
// RenderTask resource packets cannot be invalidated when task vectors grow.
class RENDER_API RenderTaskExtension {
public:
    RenderTaskExtension();
    virtual ~RenderTaskExtension();
};

class RENDER_API RenderTaskList {
    std::vector<RenderTask> tasks_;
    std::vector<std::unique_ptr<RenderTaskExtension>> extensions_;

public:
    RenderTaskList();
    RenderTaskList(const RenderTaskList&) = delete;
    RenderTaskList& operator=(const RenderTaskList&) = delete;
    RenderTaskList(RenderTaskList&&) noexcept;
    RenderTaskList& operator=(RenderTaskList&&) noexcept;

    void reserve(size_t count);
    RenderTask& append();
    RenderTask& at(size_t index);
    const RenderTask& at(size_t index) const;

    template <typename T, typename... Args>
    T& emplace_extension(Args&&... args) {
        static_assert(std::is_base_of_v<RenderTaskExtension, T>);
        auto extension = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *extension;
        extensions_.push_back(std::move(extension));
        return result;
    }

    bool empty() const;
    size_t size() const;
    std::vector<RenderTask>::iterator begin();
    std::vector<RenderTask>::iterator end();
    std::vector<RenderTask>::const_iterator begin() const;
    std::vector<RenderTask>::const_iterator end() const;
};

} // namespace termin
