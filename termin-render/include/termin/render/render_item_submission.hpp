#pragma once

#include <termin/render/material_pipeline.hpp>
#include <termin/render/render_export.hpp>

#include <array>
#include <utility>

extern "C" {
#include <core/tc_render_item.h>
#include <tgfx/resources/tc_shader.h>
}

struct tc_material_phase;

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

struct RenderContext;
struct RenderTask;
class RenderTaskList;

struct RenderItemNamedTextureBinding {
    const char* name = nullptr;
    tgfx::TextureHandle texture;
    tgfx::SamplerHandle sampler;
};

struct RenderItemNamedUniformBinding {
    const char* name = nullptr;
    const void* data = nullptr;
    uint32_t size = 0;
    const char* only_if_shader_has_resource = nullptr;
    const char* only_if_shader_lacks_resource = nullptr;
};

struct RenderItemResourceBinding {
    const MaterialPipelineResourceView* material_resources = nullptr;
    const RenderItemNamedUniformBinding* named_uniforms = nullptr;
    uint32_t named_uniform_count = 0;
    const RenderItemNamedTextureBinding* named_textures = nullptr;
    uint32_t named_texture_count = 0;
};

struct RenderItemDrawSubmitRequest {
    const tc_shader* shader = nullptr;
    tc_shader_handle shader_handle = tc_shader_handle_invalid();
    tgfx::IRenderDevice* device = nullptr;
    MaterialMeshVertexInput mesh_vertex_input = MaterialMeshVertexInput::FullMaterial;
    const RenderContext* draw_context = nullptr;
    tc_material_phase* material_phase = nullptr;
    tc_phase_mask phase = TC_PHASE_NONE;
    const char* debug_pass_name = nullptr;
    const char* debug_entity_name = nullptr;
    const RenderItemResourceBinding* resources = nullptr;
};

using RenderItemDrawEncoderFn = bool (*)(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request,
    void* user_data);

struct RenderItemEncoderCapabilities {
    tc_phase_mask phase_mask = TC_PHASE_NONE;
    uint64_t vertex_transform_kind_mask = 0;
    uint32_t supported_task_input_mask = 0;
    uint32_t required_task_input_mask = 0;
    bool requires_draw_context = false;
    bool consumes_common_resources = true;
};

enum class RenderItemMaterialPhasePolicy : uint8_t {
    Forbidden,
    Optional,
    Required,
};

enum class RenderItemTaskInput : uint32_t {
    DrawContext = 1u << 0,
    ModelMatrix = 1u << 1,
    OverrideColor = 1u << 2,
    InlineUniform = 1u << 3,
};

constexpr uint32_t render_item_task_input_bit(RenderItemTaskInput input)
{
    return static_cast<uint32_t>(input);
}

constexpr uint64_t render_item_vertex_transform_kind_bit(VertexTransformKind kind)
{
    return 1ull << static_cast<uint32_t>(kind);
}

// A pass-owned ABI contract for planning one item. Capabilities remain coarse
// discovery metadata; this packet is the authoritative compatibility request.
// The material-pipeline contract is borrowed only for the duration of planning.
struct RenderItemTaskPlanningContract {
    tc_phase_mask phase = TC_PHASE_NONE;
    RenderItemMaterialPhasePolicy material_phase_policy =
        RenderItemMaterialPhasePolicy::Optional;
    uint32_t provided_input_mask = 0;
    uint32_t required_input_mask = 0;
    uint64_t accepted_vertex_transform_kind_mask = UINT64_MAX;
    const MaterialPipelinePassContract* shader_contract = nullptr;
    const char* debug_pass_name = nullptr;
};

enum class RenderItemTaskRejection : uint8_t {
    None,
    InvalidRequest,
    EncoderNotFound,
    PassOutputUnsupported,
    MaterialPhaseRequired,
    MaterialPhaseForbidden,
    RequiredInputUnsupported,
    RequiredInputMissing,
    VertexTransformUnsupported,
    PassVertexTransformUnsupported,
    MeshVertexInputMismatch,
    ShaderPlanningRejected,
};

struct RenderItemTaskShaderPlan {
    static constexpr uint32_t MAX_SHADER_USAGES = 4u;

    tc_shader_handle final_shader = tc_shader_handle_invalid();
    tc_shader_handle shader_usages[MAX_SHADER_USAGES]{};
    std::array<TcShader, MAX_SHADER_USAGES> owned_shader_usages{};
    uint32_t shader_usage_count = 0;
    VertexTransformKind vertex_transform_kind = VertexTransformKind::StaticMesh;
    bool has_vertex_transform_kind = false;

    bool add_shader_usage(tc_shader_handle shader) {
        if (tc_shader_handle_is_invalid(shader)) {
            return true;
        }
        for (uint32_t i = 0; i < shader_usage_count; ++i) {
            if (tc_shader_handle_eq(shader_usages[i], shader)) {
                return true;
            }
        }
        if (shader_usage_count >= MAX_SHADER_USAGES) {
            return false;
        }
        shader_usages[shader_usage_count++] = shader;
        return true;
    }

    bool add_shader_usage(TcShader shader) {
        if (!shader.is_valid()) {
            return true;
        }
        for (uint32_t i = 0; i < shader_usage_count; ++i) {
            if (tc_shader_handle_eq(shader_usages[i], shader.handle)) {
                if (!owned_shader_usages[i].is_valid()) {
                    owned_shader_usages[i] = std::move(shader);
                }
                return true;
            }
        }
        if (shader_usage_count >= MAX_SHADER_USAGES) {
            return false;
        }
        shader_usages[shader_usage_count] = shader.handle;
        owned_shader_usages[shader_usage_count] = std::move(shader);
        ++shader_usage_count;
        return true;
    }

    bool set_final_shader(TcShader shader) {
        final_shader = shader.handle;
        return add_shader_usage(std::move(shader));
    }
};

struct RenderItemTaskPlanningRequest {
    const tc_render_item* item = nullptr;
    size_t item_index = SIZE_MAX;
    size_t source_draw_index = SIZE_MAX;
    tc_material_phase* material_phase = nullptr;
    tc_shader_handle candidate_shader = tc_shader_handle_invalid();
    const RenderItemTaskPlanningContract* contract = nullptr;
};

struct RenderItemTaskPlanningResult {
    size_t task_index = SIZE_MAX;
    RenderItemTaskRejection rejection = RenderItemTaskRejection::None;
    const char* detail = nullptr;

    bool accepted() const {
        return task_index != SIZE_MAX && rejection == RenderItemTaskRejection::None;
    }
};

using RenderItemTaskShaderPlannerFn = RenderItemTaskRejection (*)(
    const RenderItemTaskPlanningRequest& request,
    RenderItemTaskShaderPlan& out_plan,
    const char*& out_detail,
    void* user_data);

struct RenderItemDrawEncoderDesc {
    RenderItemDrawEncoderFn encode = nullptr;
    RenderItemTaskShaderPlannerFn plan_task_shader = nullptr;
    void* user_data = nullptr;
    const char* debug_name = nullptr;
    RenderItemEncoderCapabilities capabilities{};
};

RENDER_API bool set_render_item_inline_uniform(
    tc_render_item& item,
    const char* name,
    const void* data,
    uint32_t size);

// Registers an encoder for a non-mesh RenderItem kind owned by another package.
// Passes bind pass/material resources before submit; encoders bind only
// payload-kind-specific resources and issue backend draw commands.
RENDER_API bool register_render_item_draw_encoder(
    uint32_t item_kind,
    const RenderItemDrawEncoderDesc& desc);

RENDER_API bool unregister_render_item_draw_encoder(
    uint32_t item_kind,
    RenderItemDrawEncoderFn encode,
    void* user_data = nullptr);

RENDER_API bool get_render_item_encoder_capabilities(
    uint32_t item_kind,
    RenderItemEncoderCapabilities& out);

RENDER_API bool render_item_encoder_supports_phase(
    uint32_t item_kind,
    tc_phase_mask phase);

// Explicit shader-planning hook for encoders whose current shader candidate is
// already final. Registering this hook is intentional: #205 can replace it per
// item kind without extending Drawable or introducing a second pass protocol.
RENDER_API RenderItemTaskRejection plan_render_item_passthrough_shader(
    const RenderItemTaskPlanningRequest& request,
    RenderItemTaskShaderPlan& out_plan,
    const char*& out_detail,
    void* user_data);

// Plan and append one owned task. A rejection never mutates out_tasks and is
// always logged with pass, encoder, item kind and a structured reason.
RENDER_API RenderItemTaskPlanningResult plan_render_item_task(
    const RenderItemTaskPlanningRequest& request,
    RenderTaskList& out_tasks);

RENDER_API const char* render_item_task_rejection_name(
    RenderItemTaskRejection rejection);

RENDER_API bool bind_render_item_common_resources(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    const RenderItemDrawSubmitRequest& request);

// Bind small item-local constant-buffer data carried directly by a typed
// RenderItem. This replaces backend-specific Drawable upload callbacks while
// keeping the payload valid across deferred collection and submission.
RENDER_API bool bind_render_item_inline_uniform(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const tc_shader* shader,
    const char* debug_pass_name,
    const char* debug_entity_name);

RENDER_API bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request);

} // namespace termin
