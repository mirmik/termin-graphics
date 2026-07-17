#include <termin/render/render_item_submission.hpp>
#include <termin/render/render_task.hpp>

#include "render_item_mesh.hpp"

#include <tcbase/tc_log.hpp>

#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace termin {

bool set_render_item_inline_uniform(
    tc_render_item& item,
    const char* name,
    const void* data,
    uint32_t size)
{
    if (!name || name[0] == '\0' || !data || size == 0u ||
        size > TC_RENDER_ITEM_INLINE_UNIFORM_DATA_CAPACITY) {
        return false;
    }
    const size_t name_length = std::strlen(name);
    if (name_length >= TC_RENDER_ITEM_INLINE_UNIFORM_NAME_CAPACITY) {
        return false;
    }
    item.inline_uniform = {};
    std::memcpy(item.inline_uniform.name, name, name_length + 1u);
    std::memcpy(item.inline_uniform.data, data, size);
    item.inline_uniform.size = size;
    item.flags |= TC_RENDER_ITEM_FLAG_HAS_INLINE_UNIFORM;
    return true;
}

namespace {

struct RegisteredRenderItemDrawEncoder {
    RenderItemDrawEncoderFn encode = nullptr;
    RenderItemTaskShaderPlannerFn plan_task_shader = nullptr;
    void* user_data = nullptr;
    std::string debug_name;
    RenderItemEncoderCapabilities capabilities{};
};

std::mutex& render_item_draw_encoder_mutex()
{
    static std::mutex mutex;
    return mutex;
}

using RegisteredRenderItemDrawEncoderPtr =
    std::shared_ptr<const RegisteredRenderItemDrawEncoder>;

std::unordered_map<uint32_t, RegisteredRenderItemDrawEncoderPtr>&
render_item_draw_encoder_registry()
{
    static std::unordered_map<uint32_t, RegisteredRenderItemDrawEncoderPtr> registry;
    return registry;
}

bool find_registered_render_item_draw_encoder(
    uint32_t item_kind,
    RegisteredRenderItemDrawEncoderPtr& out_encoder)
{
    std::lock_guard<std::mutex> lock(render_item_draw_encoder_mutex());
    auto& registry = render_item_draw_encoder_registry();
    auto it = registry.find(item_kind);
    if (it == registry.end()) {
        return false;
    }
    out_encoder = it->second;
    return true;
}

bool request_has_shader_handle(const RenderItemDrawSubmitRequest& request)
{
    return !tc_shader_handle_is_invalid(request.shader_handle);
}

bool resolve_request_shader(
    tgfx::RenderContext2& ctx,
    const RenderItemDrawSubmitRequest& request,
    const char* pass_name,
    const char* entity_name,
    MaterialPipelineShaderBinding& out_binding,
    const tc_shader*& out_shader)
{
    out_binding = {};
    out_shader = nullptr;

    if (!request_has_shader_handle(request)) {
        out_shader = request.shader;
        return out_shader != nullptr;
    }

    if (!request.device) {
        tc::Log::error(
            "[%s] skip RenderItem draw for '%s': shader_handle was provided without render device",
            pass_name,
            entity_name);
        return false;
    }

    if (!ensure_material_pipeline_shader(
            ctx,
            *request.device,
            request.shader_handle,
            pass_name,
            out_binding)) {
        return false;
    }
    out_shader = out_binding.shader;
    return out_shader != nullptr;
}

bool bind_request_resources(
    tgfx::RenderContext2& ctx,
    const RenderItemDrawSubmitRequest& request,
    const tc_shader* shader,
    const char* pass_name,
    const char* entity_name)
{
    if (request.resources == nullptr) {
        return true;
    }
    const RenderItemResourceBinding& resources = *request.resources;
    if (resources.material_resources && !request.device) {
        tc::Log::error(
            "[%s] skip RenderItem draw for '%s': material resource binding was provided without render device",
            pass_name,
            entity_name);
        return false;
    }
    if (resources.material_resources) {
        prepare_material_pipeline_resources(
            ctx,
            *request.device,
            shader,
            request.material_phase,
            *resources.material_resources);
    }
    if (resources.named_uniforms) {
        for (uint32_t i = 0; i < resources.named_uniform_count; ++i) {
            const RenderItemNamedUniformBinding& uniform = resources.named_uniforms[i];
            if (!uniform.name || !uniform.data || uniform.size == 0) {
                continue;
            }
            if (uniform.only_if_shader_has_resource &&
                !tc_shader_find_resource_binding(shader, uniform.only_if_shader_has_resource)) {
                continue;
            }
            if (uniform.only_if_shader_lacks_resource &&
                tc_shader_find_resource_binding(shader, uniform.only_if_shader_lacks_resource)) {
                continue;
            }
            ctx.bind_uniform_data(uniform.name, uniform.data, uniform.size);
        }
    }
    if (resources.named_textures) {
        for (uint32_t i = 0; i < resources.named_texture_count; ++i) {
            const RenderItemNamedTextureBinding& texture = resources.named_textures[i];
            if (!texture.name || !texture.texture) {
                continue;
            }
            const tc_shader_resource_binding* rb =
                tc_shader_find_resource_binding(shader, texture.name);
            if (!rb || rb->kind != TC_SHADER_RESOURCE_TEXTURE) {
                tc::Log::error(
                    "[%s] skip extra texture '%s' for '%s': shader does not declare a Texture2D resource with that name",
                    pass_name,
                    texture.name,
                    entity_name);
                continue;
            }
            ctx.bind_texture(texture.name, texture.texture, texture.sampler);
        }
    }
    return true;
}

bool mesh_render_item_draw_encoder(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request,
    void* user_data)
{
    (void)user_data;

    const char* pass_name = request.debug_pass_name
        ? request.debug_pass_name
        : "RenderItemSubmit";
    const char* entity_name = request.debug_entity_name
        ? request.debug_entity_name
        : "<unnamed>";

    MaterialPipelineShaderBinding binding{};
    const tc_shader* shader = nullptr;
    if (!resolve_request_shader(ctx, request, pass_name, entity_name, binding, shader)) {
        tc::Log::error(
            "[%s] skip RenderItem mesh draw for '%s': request has no shader",
            pass_name,
            entity_name);
        return false;
    }
    if (!bind_request_resources(
            ctx,
            request,
            shader,
            pass_name,
            entity_name)) {
        return false;
    }
    if (!bind_render_item_inline_uniform(
            ctx,
            item,
            shader,
            pass_name,
            entity_name)) {
        return false;
    }

    MeshRenderItemEncodeRequest mesh_request{};
    mesh_request.shader = shader;
    mesh_request.vertex_input = request.mesh_vertex_input;
    mesh_request.debug_pass_name = pass_name;
    mesh_request.debug_entity_name = entity_name;
    return encode_mesh_render_item_draw(ctx, item, mesh_request);
}

RenderItemEncoderCapabilities mesh_render_item_capabilities()
{
    RenderItemEncoderCapabilities capabilities{};
    capabilities.pass_semantic_mask =
        render_item_pass_semantic_bit(RenderItemPassSemantic::Color)
        | render_item_pass_semantic_bit(RenderItemPassSemantic::Shadow)
        | render_item_pass_semantic_bit(RenderItemPassSemantic::Id)
        | render_item_pass_semantic_bit(RenderItemPassSemantic::Depth)
        | render_item_pass_semantic_bit(RenderItemPassSemantic::DepthOnly)
        | render_item_pass_semantic_bit(RenderItemPassSemantic::Normal);
    capabilities.vertex_transform_kind_mask =
        render_item_vertex_transform_kind_bit(VertexTransformKind::StaticMesh)
        | render_item_vertex_transform_kind_bit(VertexTransformKind::SkinnedMesh);
    capabilities.supported_task_input_mask =
        render_item_task_input_bit(RenderItemTaskInput::DrawContext)
        | render_item_task_input_bit(RenderItemTaskInput::ModelMatrix)
        | render_item_task_input_bit(RenderItemTaskInput::InlineUniform);
    capabilities.required_task_input_mask =
        render_item_task_input_bit(RenderItemTaskInput::DrawContext);
    capabilities.requires_draw_context = true;
    capabilities.consumes_common_resources = true;
    return capabilities;
}

RenderItemTaskRejection mesh_render_item_task_shader_planner(
    const RenderItemTaskPlanningRequest& request,
    RenderItemTaskShaderPlan& out_plan,
    const char*& out_detail,
    void* user_data)
{
    (void)user_data;
    if (!request.item || request.item->kind != TC_RENDER_ITEM_KIND_MESH) {
        out_detail = "mesh planner received a non-mesh item";
        return RenderItemTaskRejection::ShaderPlanningRejected;
    }
    out_plan.final_shader = request.candidate_shader;
    out_plan.has_vertex_transform_kind = true;
    out_plan.vertex_transform_kind =
        (request.item->flags & TC_RENDER_ITEM_FLAG_HAS_SKINNING_MATRICES)
        ? VertexTransformKind::SkinnedMesh
        : VertexTransformKind::StaticMesh;
    out_detail = nullptr;
    return RenderItemTaskRejection::None;
}

void ensure_builtin_render_item_draw_encoders()
{
    static std::once_flag once;
    std::call_once(once, []() {
        auto mesh_encoder = std::make_shared<RegisteredRenderItemDrawEncoder>();
        mesh_encoder->encode = mesh_render_item_draw_encoder;
        mesh_encoder->plan_task_shader = mesh_render_item_task_shader_planner;
        mesh_encoder->debug_name = "MeshRenderItem";
        mesh_encoder->capabilities = mesh_render_item_capabilities();

        std::lock_guard<std::mutex> lock(render_item_draw_encoder_mutex());
        render_item_draw_encoder_registry().emplace(
            TC_RENDER_ITEM_KIND_MESH,
            mesh_encoder);
    });
}

} // namespace

bool register_render_item_draw_encoder(
    uint32_t item_kind,
    const RenderItemDrawEncoderDesc& desc)
{
    if (item_kind == TC_RENDER_ITEM_KIND_INVALID) {
        tc::Log::error(
            "[RenderItemSubmit] cannot register draw encoder for invalid item kind");
        return false;
    }
    if (!desc.encode) {
        tc::Log::error(
            "[RenderItemSubmit] cannot register null draw encoder for item kind %u",
            item_kind);
        return false;
    }
    if (!desc.plan_task_shader) {
        tc::Log::error(
            "[RenderItemSubmit] cannot register encoder without task shader planner for item kind %u",
            item_kind);
        return false;
    }

    ensure_builtin_render_item_draw_encoders();

    std::lock_guard<std::mutex> lock(render_item_draw_encoder_mutex());
    auto& registry = render_item_draw_encoder_registry();
    if (registry.find(item_kind) != registry.end()) {
        tc::Log::error(
            "[RenderItemSubmit] draw encoder for item kind %u is already registered",
            item_kind);
        return false;
    }

    auto registered = std::make_shared<RegisteredRenderItemDrawEncoder>();
    registered->encode = desc.encode;
    registered->plan_task_shader = desc.plan_task_shader;
    registered->user_data = desc.user_data;
    registered->debug_name = desc.debug_name ? desc.debug_name : "";
    registered->capabilities = desc.capabilities;
    registry.emplace(item_kind, registered);
    return true;
}

bool unregister_render_item_draw_encoder(
    uint32_t item_kind,
    RenderItemDrawEncoderFn encode,
    void* user_data)
{
    ensure_builtin_render_item_draw_encoders();

    if (item_kind == TC_RENDER_ITEM_KIND_MESH) {
        tc::Log::error(
            "[RenderItemSubmit] cannot unregister built-in mesh draw encoder");
        return false;
    }

    std::lock_guard<std::mutex> lock(render_item_draw_encoder_mutex());
    auto& registry = render_item_draw_encoder_registry();
    auto it = registry.find(item_kind);
    if (it == registry.end()) {
        tc::Log::error(
            "[RenderItemSubmit] cannot unregister draw encoder for item kind %u: no encoder registered",
            item_kind);
        return false;
    }
    if (it->second->encode != encode || it->second->user_data != user_data) {
        tc::Log::error(
            "[RenderItemSubmit] cannot unregister draw encoder for item kind %u: callback mismatch",
            item_kind);
        return false;
    }
    registry.erase(it);
    return true;
}

bool get_render_item_encoder_capabilities(
    uint32_t item_kind,
    RenderItemEncoderCapabilities& out)
{
    ensure_builtin_render_item_draw_encoders();

    RegisteredRenderItemDrawEncoderPtr encoder;
    if (!find_registered_render_item_draw_encoder(item_kind, encoder)) {
        out = {};
        return false;
    }
    out = encoder->capabilities;
    return true;
}

bool render_item_encoder_supports_pass(
    uint32_t item_kind,
    RenderItemPassSemantic semantic)
{
    RenderItemEncoderCapabilities capabilities{};
    if (!get_render_item_encoder_capabilities(item_kind, capabilities)) {
        return false;
    }
    return (capabilities.pass_semantic_mask & render_item_pass_semantic_bit(semantic)) != 0;
}

const char* render_item_task_rejection_name(RenderItemTaskRejection rejection)
{
    switch (rejection) {
    case RenderItemTaskRejection::None: return "none";
    case RenderItemTaskRejection::InvalidRequest: return "invalid_request";
    case RenderItemTaskRejection::EncoderNotFound: return "encoder_not_found";
    case RenderItemTaskRejection::PassOutputUnsupported: return "pass_output_unsupported";
    case RenderItemTaskRejection::MaterialPhaseRequired: return "material_phase_required";
    case RenderItemTaskRejection::MaterialPhaseForbidden: return "material_phase_forbidden";
    case RenderItemTaskRejection::RequiredInputUnsupported: return "required_input_unsupported";
    case RenderItemTaskRejection::RequiredInputMissing: return "required_input_missing";
    case RenderItemTaskRejection::VertexTransformUnsupported: return "vertex_transform_unsupported";
    case RenderItemTaskRejection::PassVertexTransformUnsupported: return "pass_vertex_transform_unsupported";
    case RenderItemTaskRejection::ShaderPlanningRejected: return "shader_planning_rejected";
    }
    return "unknown";
}

RenderItemTaskRejection plan_render_item_passthrough_shader(
    const RenderItemTaskPlanningRequest& request,
    RenderItemTaskShaderPlan& out_plan,
    const char*& out_detail,
    void* user_data)
{
    (void)user_data;
    out_plan = {};
    out_plan.final_shader = request.candidate_shader;
    out_detail = nullptr;
    return RenderItemTaskRejection::None;
}

namespace {

uint32_t render_item_available_task_inputs(
    const tc_render_item& item,
    const RenderItemTaskPlanningContract& contract)
{
    uint32_t inputs = contract.provided_input_mask;
    if ((item.flags & TC_RENDER_ITEM_FLAG_HAS_MODEL_MATRIX) != 0u) {
        inputs |= render_item_task_input_bit(RenderItemTaskInput::ModelMatrix);
    }
    if ((item.flags & TC_RENDER_ITEM_FLAG_HAS_OVERRIDE_COLOR) != 0u) {
        inputs |= render_item_task_input_bit(RenderItemTaskInput::OverrideColor);
    }
    if ((item.flags & TC_RENDER_ITEM_FLAG_HAS_INLINE_UNIFORM) != 0u) {
        inputs |= render_item_task_input_bit(RenderItemTaskInput::InlineUniform);
    }
    return inputs;
}

void log_task_rejection(
    const RenderItemTaskPlanningRequest& request,
    const RegisteredRenderItemDrawEncoder* encoder,
    RenderItemTaskPlanningResult& result)
{
    const char* pass_name = request.contract && request.contract->debug_pass_name
        ? request.contract->debug_pass_name
        : "<unknown>";
    const char* encoder_name = encoder && !encoder->debug_name.empty()
        ? encoder->debug_name.c_str()
        : "<unregistered>";
    tc::Log::error(
        "[RenderItemPlan] reject task: pass='%s' item_kind=%u encoder='%s' reason='%s' detail='%s'",
        pass_name,
        request.item
            ? request.item->kind
            : static_cast<uint32_t>(TC_RENDER_ITEM_KIND_INVALID),
        encoder_name,
        render_item_task_rejection_name(result.rejection),
        result.detail ? result.detail : "");
}

} // namespace

RenderItemTaskPlanningResult plan_render_item_task(
    const RenderItemTaskPlanningRequest& request,
    RenderTaskList& out_tasks)
{
    RenderItemTaskPlanningResult result{};
    if (!request.item || !request.contract) {
        result.rejection = RenderItemTaskRejection::InvalidRequest;
        result.detail = "item and planning contract are required";
        log_task_rejection(request, nullptr, result);
        return result;
    }

    ensure_builtin_render_item_draw_encoders();
    RegisteredRenderItemDrawEncoderPtr encoder;
    if (!find_registered_render_item_draw_encoder(request.item->kind, encoder)) {
        result.rejection = RenderItemTaskRejection::EncoderNotFound;
        result.detail = "no encoder is registered for the item kind";
        log_task_rejection(request, nullptr, result);
        return result;
    }

    const RenderItemTaskPlanningContract& contract = *request.contract;
    if ((encoder->capabilities.pass_semantic_mask &
         render_item_pass_semantic_bit(contract.pass_semantic)) == 0u) {
        result.rejection = RenderItemTaskRejection::PassOutputUnsupported;
        result.detail = "encoder does not advertise the requested pass output ABI";
        log_task_rejection(request, encoder.get(), result);
        return result;
    }
    if (contract.material_phase_policy == RenderItemMaterialPhasePolicy::Required &&
        !request.material_phase) {
        result.rejection = RenderItemTaskRejection::MaterialPhaseRequired;
        result.detail = "pass requires a resolved material phase";
        log_task_rejection(request, encoder.get(), result);
        return result;
    }
    if (contract.material_phase_policy == RenderItemMaterialPhasePolicy::Forbidden &&
        request.material_phase) {
        result.rejection = RenderItemTaskRejection::MaterialPhaseForbidden;
        result.detail = "pass forbids material-phase participation";
        log_task_rejection(request, encoder.get(), result);
        return result;
    }
    if ((contract.required_input_mask & ~encoder->capabilities.supported_task_input_mask) != 0u) {
        result.rejection = RenderItemTaskRejection::RequiredInputUnsupported;
        result.detail = "encoder cannot consume a pass-required task input";
        log_task_rejection(request, encoder.get(), result);
        return result;
    }
    const uint32_t available_inputs =
        render_item_available_task_inputs(*request.item, contract);
    const uint32_t required_inputs =
        encoder->capabilities.required_task_input_mask | contract.required_input_mask;
    if ((required_inputs & ~available_inputs) != 0u) {
        result.rejection = RenderItemTaskRejection::RequiredInputMissing;
        result.detail = "item/pass packet does not provide an encoder-required input";
        log_task_rejection(request, encoder.get(), result);
        return result;
    }

    RenderItemTaskShaderPlan shader_plan{};
    const char* planner_detail = nullptr;
    RenderItemTaskRejection planner_rejection = encoder->plan_task_shader(
        request,
        shader_plan,
        planner_detail,
        encoder->user_data);
    if (planner_rejection != RenderItemTaskRejection::None) {
        result.rejection = planner_rejection;
        result.detail = planner_detail ? planner_detail : "encoder shader planner rejected the task";
        log_task_rejection(request, encoder.get(), result);
        return result;
    }
    if (tc_shader_handle_is_invalid(shader_plan.final_shader)) {
        result.rejection = RenderItemTaskRejection::ShaderPlanningRejected;
        result.detail = "encoder shader planner produced an invalid final shader";
        log_task_rejection(request, encoder.get(), result);
        return result;
    }
    if (shader_plan.has_vertex_transform_kind) {
        const uint64_t transform_bit =
            render_item_vertex_transform_kind_bit(shader_plan.vertex_transform_kind);
        if ((encoder->capabilities.vertex_transform_kind_mask & transform_bit) == 0u) {
            result.rejection = RenderItemTaskRejection::VertexTransformUnsupported;
            result.detail = "encoder planner selected a transform outside encoder capabilities";
            log_task_rejection(request, encoder.get(), result);
            return result;
        }
        if ((contract.accepted_vertex_transform_kind_mask & transform_bit) == 0u) {
            result.rejection = RenderItemTaskRejection::PassVertexTransformUnsupported;
            result.detail = "pass contract does not accept the selected vertex transform";
            log_task_rejection(request, encoder.get(), result);
            return result;
        }
    }

    const size_t task_index = out_tasks.size();
    RenderTask& task = out_tasks.append();
    task.source_draw_index = request.source_draw_index;
    task.item_index = request.item_index;
    task.item = request.item;
    task.material_phase = request.material_phase;
    task.final_shader = shader_plan.final_shader;
    task.pass_semantic = contract.pass_semantic;
    task.has_vertex_transform_kind = shader_plan.has_vertex_transform_kind;
    task.vertex_transform_kind = shader_plan.vertex_transform_kind;
    result.task_index = task_index;
    return result;
}

bool bind_render_item_common_resources(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    const RenderItemDrawSubmitRequest& request)
{
    const char* pass_name = request.debug_pass_name
        ? request.debug_pass_name
        : "RenderItemSubmit";
    const char* entity_name = request.debug_entity_name
        ? request.debug_entity_name
        : "<unnamed>";
    if (!shader) {
        tc::Log::error(
            "[%s] cannot bind RenderItem common resources for '%s': shader layout is null",
            pass_name,
            entity_name);
        return false;
    }
    return bind_request_resources(
        ctx,
        request,
        shader,
        pass_name,
        entity_name);
}

bool bind_render_item_inline_uniform(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const tc_shader* shader,
    const char* debug_pass_name,
    const char* debug_entity_name)
{
    if ((item.flags & TC_RENDER_ITEM_FLAG_HAS_INLINE_UNIFORM) == 0u) {
        return true;
    }
    const char* pass_name = debug_pass_name ? debug_pass_name : "RenderItemSubmit";
    const char* entity_name = debug_entity_name ? debug_entity_name : "<unnamed>";
    const tc_render_item_inline_uniform& uniform = item.inline_uniform;
    if (!shader) {
        tc::Log::error(
            "[%s] cannot bind inline uniform '%s' for '%s': shader layout is null",
            pass_name,
            uniform.name,
            entity_name);
        return false;
    }
    const tc_shader_resource_binding* binding =
        tc_shader_find_resource_binding(shader, uniform.name);
    if (!binding || binding->kind != TC_SHADER_RESOURCE_CONSTANT_BUFFER) {
        tc::Log::error(
            "[%s] cannot bind inline uniform '%s' for '%s': shader has no matching constant buffer",
            pass_name,
            uniform.name,
            entity_name);
        return false;
    }
    ctx.bind_uniform_data(uniform.name, uniform.data, uniform.size);
    return true;
}

bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request)
{
    const char* pass_name = request.debug_pass_name
        ? request.debug_pass_name
        : "RenderItemSubmit";
    const char* entity_name = request.debug_entity_name
        ? request.debug_entity_name
        : "<unnamed>";

    ensure_builtin_render_item_draw_encoders();

    RegisteredRenderItemDrawEncoderPtr encoder;
    if (find_registered_render_item_draw_encoder(item.kind, encoder)) {
        return encoder->encode(ctx, item, request, encoder->user_data);
    }
    tc::Log::error(
        "[%s] skip RenderItem draw for '%s': unsupported item kind %u",
        pass_name,
        entity_name,
        item.kind);
    return false;
}

} // namespace termin
