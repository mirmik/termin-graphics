#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/render_item_submission.hpp>
#include <termin/render/render_task.hpp>

#include <array>
#include <cstdio>
#include <cstring>

namespace {

bool test_encoder(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const termin::RenderItemDrawSubmitRequest& request,
    void* user_data)
{
    (void)ctx;
    (void)item;
    (void)request;
    (void)user_data;
    return true;
}

bool other_test_encoder(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const termin::RenderItemDrawSubmitRequest& request,
    void* user_data)
{
    (void)ctx;
    (void)item;
    (void)request;
    (void)user_data;
    // Keep this callback observably different from test_encoder: MSVC's
    // identical COMDAT folding otherwise aliases their function pointers.
    return false;
}

termin::RenderItemTaskRejection selecting_test_shader_planner(
    const termin::RenderItemTaskPlanningRequest& request,
    termin::RenderItemTaskShaderPlan& out_plan,
    const char*& out_detail,
    void* user_data)
{
    (void)user_data;
    if (!request.contract || !request.contract->shader_contract) {
        out_detail = "missing pass-owned shader contract";
        return termin::RenderItemTaskRejection::ShaderPlanningRejected;
    }
    out_plan.final_shader = request.candidate_shader;
    ++out_plan.final_shader.index;
    out_detail = nullptr;
    return termin::RenderItemTaskRejection::None;
}

termin::RenderItemTaskRejection owning_test_shader_planner(
    const termin::RenderItemTaskPlanningRequest& request,
    termin::RenderItemTaskShaderPlan& out_plan,
    const char*& out_detail,
    void* user_data)
{
    (void)request;
    (void)user_data;
    termin::TcShaderCreateInfo create_info{};
    create_info.sources.vertex = "void main() {}";
    create_info.sources.fragment = "void main() {}";
    create_info.sources.name = "RenderTaskOwnedShader";
    create_info.uuid = "termin-render-task-owned-shader-test";
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    if (!out_plan.set_final_shader(termin::TcShader::from_sources(create_info))) {
        out_detail = "could not retain the planned shader";
        return termin::RenderItemTaskRejection::ShaderPlanningRejected;
    }
    out_detail = nullptr;
    return termin::RenderItemTaskRejection::None;
}

} // namespace

TEST_CASE("RenderItem draw encoder registry enforces ownership") {
    constexpr uint32_t test_kind = 0x7fff0001u;

    termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr);

    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.plan_task_shader = termin::plan_render_item_passthrough_shader;
    desc.debug_name = "test_encoder";

    CHECK(termin::register_render_item_draw_encoder(test_kind, desc));
    CHECK(!termin::register_render_item_draw_encoder(test_kind, desc));
    CHECK(!termin::unregister_render_item_draw_encoder(test_kind, other_test_encoder, nullptr));
    CHECK(termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
    CHECK(!termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
}

TEST_CASE("RenderItem draw encoder registry rejects invalid descriptors") {
    termin::RenderItemDrawEncoderDesc desc{};
    CHECK(!termin::register_render_item_draw_encoder(TC_RENDER_ITEM_KIND_INVALID, desc));

    constexpr uint32_t test_kind = 0x7fff0002u;
    CHECK(!termin::register_render_item_draw_encoder(test_kind, desc));
}

TEST_CASE("RenderItem draw encoder registry reserves built-in mesh encoder") {
    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.plan_task_shader = termin::plan_render_item_passthrough_shader;
    desc.debug_name = "replacement_mesh_encoder";

    CHECK(!termin::register_render_item_draw_encoder(TC_RENDER_ITEM_KIND_MESH, desc));
    CHECK(!termin::unregister_render_item_draw_encoder(
        TC_RENDER_ITEM_KIND_MESH,
        test_encoder,
        nullptr));
}

TEST_CASE("RenderItem draw encoder registry exposes built-in mesh capabilities") {
    termin::RenderItemEncoderCapabilities capabilities{};
    REQUIRE(termin::get_render_item_encoder_capabilities(
        TC_RENDER_ITEM_KIND_MESH,
        capabilities));
    CHECK(termin::render_item_encoder_supports_phase(
        TC_RENDER_ITEM_KIND_MESH,
        TC_PHASE_OPAQUE));
    CHECK(termin::render_item_encoder_supports_phase(
        TC_RENDER_ITEM_KIND_MESH,
        TC_PHASE_SHADOW));
    CHECK(termin::render_item_encoder_supports_phase(
        TC_RENDER_ITEM_KIND_MESH,
        TC_PHASE_ID));
    CHECK(termin::render_item_encoder_supports_phase(
        TC_RENDER_ITEM_KIND_MESH,
        TC_PHASE_DEPTH));
    CHECK(termin::render_item_encoder_supports_phase(
        TC_RENDER_ITEM_KIND_MESH,
        TC_PHASE_NORMAL));
    CHECK(capabilities.requires_draw_context);
    CHECK(capabilities.consumes_common_resources);
}

TEST_CASE("RenderItem draw encoder registry stores custom capabilities") {
    constexpr uint32_t test_kind = 0x7fff0003u;

    termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr);

    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.plan_task_shader = termin::plan_render_item_passthrough_shader;
    desc.debug_name = "capability_test_encoder";
    desc.capabilities.phase_mask = TC_PHASE_OPAQUE | TC_PHASE_SHADOW;
    desc.capabilities.requires_draw_context = true;
    desc.capabilities.consumes_common_resources = false;
    desc.capabilities.supported_task_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
    desc.capabilities.required_task_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);

    REQUIRE(termin::register_render_item_draw_encoder(test_kind, desc));

    termin::RenderItemEncoderCapabilities capabilities{};
    REQUIRE(termin::get_render_item_encoder_capabilities(test_kind, capabilities));
    CHECK(termin::render_item_encoder_supports_phase(
        test_kind,
        TC_PHASE_OPAQUE));
    CHECK(termin::render_item_encoder_supports_phase(
        test_kind,
        TC_PHASE_SHADOW));
    CHECK(!termin::render_item_encoder_supports_phase(
        test_kind,
        TC_PHASE_DEPTH));
    CHECK(capabilities.requires_draw_context);
    CHECK(!capabilities.consumes_common_resources);

    CHECK(termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
    CHECK(!termin::get_render_item_encoder_capabilities(test_kind, capabilities));
}

TEST_CASE("RenderItem task planner accepts a supported mesh contract") {
    tc_shader_init();

    termin::TcShaderCreateInfo create_info{};
    create_info.sources.fragment = R"(
struct FragmentOutput { float4 color : SV_Target0; };
[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}
)";
    create_info.sources.name = "static-mesh-planner-material";
    create_info.sources.fragment_entry = "fs_main";
    create_info.uuid = "static-mesh-planner-material";
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    termin::TcShader candidate = termin::TcShader::from_sources(create_info);
    REQUIRE(candidate.is_valid());

    tc_shader_resource_binding material_layout{};
    std::snprintf(
        material_layout.name,
        sizeof(material_layout.name),
        "%s",
        TC_SHADER_RESOURCE_MATERIAL);
    material_layout.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    material_layout.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    material_layout.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    material_layout.size = 64u;
    tc_shader_set_resource_layout(candidate.get(), &material_layout, 1u);

    // Reproduce the authored/reflection contract that triggered the runtime
    // regression.  The mesh planner must not pass this original all-graphics
    // requirement through to a fragment-only material layout.
    tc_shader_resource_requirement authored_material{};
    std::snprintf(
        authored_material.name,
        sizeof(authored_material.name),
        "%s",
        TC_SHADER_RESOURCE_MATERIAL);
    authored_material.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    authored_material.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    authored_material.stage_mask = TC_SHADER_STAGE_ALL_GRAPHICS;
    authored_material.size = 64u;
    tc_shader_contract_desc authored_contract{};
    authored_contract.schema_version = TC_SHADER_CONTRACT_SCHEMA_VERSION;
    authored_contract.source_kind = TC_SHADER_CONTRACT_SOURCE_REFLECTION;
    authored_contract.resources = &authored_material;
    authored_contract.resource_count = 1u;
    authored_contract.debug_name = "static-mesh-planner-authored-contract";
    REQUIRE(tc_shader_set_contract(candidate.get(), &authored_contract));

    tc_render_item item{};
    item.kind = TC_RENDER_ITEM_KIND_MESH;
    item.flags = TC_RENDER_ITEM_FLAG_HAS_MODEL_MATRIX;
    tc_material_phase phase{};

    termin::MaterialPipelinePassContract shader_contract{};
    shader_contract.debug_name = "planner_test";
    shader_contract.uses_material_fragment = true;
    shader_contract.vertex_output_adapter =
        termin::material_pipeline_standard_material_vertex_output_adapter();
    shader_contract.static_vertex_transform =
        termin::material_pipeline_make_static_mesh_vertex_transform_provider(
            "static",
            termin::MeshVertexTransformProfile::Material,
            "draw_data.u_model");
    shader_contract.static_vertex_transform->resources.push_back(
        termin::material_pipeline_draw_resource_decl(
            "draw_data", TC_SHADER_STAGE_VERTEX, 64u));
    termin::RenderItemTaskPlanningContract contract{};
    contract.phase = TC_PHASE_OPAQUE;
    contract.material_phase_policy = termin::RenderItemMaterialPhasePolicy::Required;
    contract.provided_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
    contract.required_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
    contract.accepted_vertex_transform_kind_mask =
        termin::render_item_vertex_transform_kind_bit(termin::VertexTransformKind::StaticMesh);
    contract.shader_contract = &shader_contract;
    contract.debug_pass_name = "planner_test";

    termin::RenderItemTaskPlanningRequest request{};
    request.item = &item;
    request.item_index = 7u;
    request.source_draw_index = 3u;
    request.material_phase = &phase;
    request.candidate_shader = candidate.handle;
    request.contract = &contract;

    termin::RenderTaskList tasks;
    termin::RenderItemTaskPlanningResult result =
        termin::plan_render_item_task(request, tasks);
    REQUIRE(result.accepted());
    REQUIRE(tasks.size() == 1u);
    const termin::RenderTask& task = tasks.at(result.task_index);
    CHECK(task.item == &item);
    CHECK(task.item_index == 7u);
    CHECK(task.source_draw_index == 3u);
    CHECK(task.material_phase == &phase);
    CHECK(task.phase == TC_PHASE_OPAQUE);
    CHECK(task.has_vertex_transform_kind);
    CHECK(task.vertex_transform_kind == termin::VertexTransformKind::StaticMesh);
    CHECK_FALSE(tc_shader_handle_eq(task.final_shader, candidate.handle));
    tc_shader* planned_shader = tc_shader_get(task.final_shader);
    REQUIRE(planned_shader != nullptr);
    tc_shader_contract_view planned_contract{};
    REQUIRE(tc_shader_get_contract_view(planned_shader, &planned_contract));
    REQUIRE(planned_contract.resource_count > 0u);
    const tc_shader_resource_requirement* planned_material = nullptr;
    for (uint32_t i = 0; i < planned_contract.resource_count; ++i) {
        if (std::strcmp(
                planned_contract.resources[i].name,
                TC_SHADER_RESOURCE_MATERIAL) == 0) {
            planned_material = &planned_contract.resources[i];
            break;
        }
    }
    REQUIRE(planned_material != nullptr);
    CHECK(planned_material->stage_mask == TC_SHADER_STAGE_FRAGMENT);

    tc_shader_shutdown();
}

TEST_CASE("RenderItem task planner delegates final shader selection to the encoder hook") {
    constexpr uint32_t test_kind = 0x7fff0004u;
    termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr);

    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.plan_task_shader = selecting_test_shader_planner;
    desc.debug_name = "selecting_test_encoder";
    desc.capabilities.phase_mask = TC_PHASE_OPAQUE;
    REQUIRE(termin::register_render_item_draw_encoder(test_kind, desc));

    tc_render_item item{};
    item.kind = test_kind;
    termin::MaterialPipelinePassContract shader_contract{};
    termin::RenderItemTaskPlanningContract contract{};
    contract.phase = TC_PHASE_OPAQUE;
    contract.shader_contract = &shader_contract;
    contract.debug_pass_name = "shader_planner_hook_test";

    termin::RenderItemTaskPlanningRequest request{};
    request.item = &item;
    request.candidate_shader.index = 10u;
    request.candidate_shader.generation = 3u;
    request.contract = &contract;

    termin::RenderTaskList tasks;
    auto result = termin::plan_render_item_task(request, tasks);
    REQUIRE(result.accepted());
    const termin::RenderTask& task = tasks.at(result.task_index);
    CHECK(task.final_shader.index == 11u);
    CHECK(task.final_shader.generation == 3u);

    CHECK(termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
}

TEST_CASE("RenderItem task shaders can be retained by pass draw-call caches") {
    constexpr uint32_t test_kind = 0x7fff0005u;
    constexpr const char* shader_uuid = "termin-render-task-owned-shader-test";
    termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr);
    tc_shader_handle stale = tc_shader_find(shader_uuid);
    if (!tc_shader_handle_is_invalid(stale)) {
        tc_shader_destroy(stale);
    }

    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.plan_task_shader = owning_test_shader_planner;
    desc.debug_name = "owning_test_encoder";
    desc.capabilities.phase_mask = TC_PHASE_OPAQUE;
    REQUIRE(termin::register_render_item_draw_encoder(test_kind, desc));

    tc_render_item item{};
    item.kind = test_kind;
    termin::RenderItemTaskPlanningContract contract{};
    contract.phase = TC_PHASE_OPAQUE;
    contract.debug_pass_name = "owned_shader_planner_test";

    termin::RenderItemTaskPlanningRequest request{};
    request.item = &item;
    request.contract = &contract;

    tc_shader_handle planned_handle = tc_shader_handle_invalid();
    termin::TcShader cached_shader;
    {
        termin::RenderTaskList tasks;
        auto result = termin::plan_render_item_task(request, tasks);
        REQUIRE(result.accepted());
        planned_handle = tasks.at(result.task_index).final_shader;
        CHECK(tc_shader_is_valid(planned_handle));
        CHECK(tasks.at(result.task_index).shader_usage_count == 1u);
        CHECK(tc_shader_handle_eq(
            tasks.at(result.task_index).shader_usages[0],
            planned_handle));
        cached_shader = termin::TcShader(planned_handle);
    }
    CHECK(tc_shader_is_valid(planned_handle));
    cached_shader = termin::TcShader();
    CHECK(!tc_shader_is_valid(planned_handle));

    CHECK(termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
}

TEST_CASE("RenderItem task planner rejects material input transform and output mismatches") {
    tc_render_item item{};
    item.kind = TC_RENDER_ITEM_KIND_MESH;
    item.flags = TC_RENDER_ITEM_FLAG_HAS_SKINNING_MATRICES;

    termin::RenderItemTaskPlanningContract contract{};
    contract.phase = TC_PHASE_OPAQUE;
    contract.material_phase_policy = termin::RenderItemMaterialPhasePolicy::Required;
    contract.provided_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
    contract.accepted_vertex_transform_kind_mask =
        termin::render_item_vertex_transform_kind_bit(termin::VertexTransformKind::StaticMesh);
    contract.debug_pass_name = "planner_rejection_test";

    termin::RenderItemTaskPlanningRequest request{};
    request.item = &item;
    request.contract = &contract;
    request.candidate_shader.index = 1u;
    request.candidate_shader.generation = 1u;
    termin::RenderTaskList tasks;

    auto missing_phase = termin::plan_render_item_task(request, tasks);
    CHECK(missing_phase.rejection == termin::RenderItemTaskRejection::MaterialPhaseRequired);
    CHECK(tasks.empty());

    tc_material_phase phase{};
    request.material_phase = &phase;
    contract.material_phase_policy = termin::RenderItemMaterialPhasePolicy::Forbidden;
    auto forbidden_phase = termin::plan_render_item_task(request, tasks);
    CHECK(forbidden_phase.rejection == termin::RenderItemTaskRejection::MaterialPhaseForbidden);
    CHECK(tasks.empty());

    contract.material_phase_policy = termin::RenderItemMaterialPhasePolicy::Required;
    contract.provided_input_mask = 0u;
    auto missing_input = termin::plan_render_item_task(request, tasks);
    CHECK(missing_input.rejection == termin::RenderItemTaskRejection::RequiredInputMissing);
    CHECK(tasks.empty());

    contract.provided_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
    contract.required_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::OverrideColor);
    auto unsupported_input = termin::plan_render_item_task(request, tasks);
    CHECK(unsupported_input.rejection == termin::RenderItemTaskRejection::RequiredInputUnsupported);
    CHECK(tasks.empty());

    contract.required_input_mask = 0u;
    auto unsupported_transform = termin::plan_render_item_task(request, tasks);
    CHECK(unsupported_transform.rejection ==
          termin::RenderItemTaskRejection::PassVertexTransformUnsupported);
    CHECK(tasks.empty());

    item.flags = 0u;
    contract.accepted_vertex_transform_kind_mask = UINT64_MAX;
    contract.phase = TC_PHASE_OPAQUE | TC_PHASE_SHADOW;
    auto unsupported_output = termin::plan_render_item_task(request, tasks);
    CHECK(unsupported_output.rejection == termin::RenderItemTaskRejection::PassOutputUnsupported);
    CHECK(tasks.empty());
}

TEST_CASE("RenderItem inline uniform owns validated item-local bytes") {
    tc_render_item item{};
    const std::array<uint32_t, 4> payload{{1u, 2u, 3u, 4u}};
    REQUIRE(termin::set_render_item_inline_uniform(
        item,
        "chrono_effect_draw",
        payload.data(),
        static_cast<uint32_t>(sizeof(payload))));
    CHECK((item.flags & TC_RENDER_ITEM_FLAG_HAS_INLINE_UNIFORM) != 0u);
    CHECK(std::strcmp(item.inline_uniform.name, "chrono_effect_draw") == 0);
    CHECK(item.inline_uniform.size == sizeof(payload));
    CHECK(std::memcmp(item.inline_uniform.data, payload.data(), sizeof(payload)) == 0);

    std::array<uint8_t, TC_RENDER_ITEM_INLINE_UNIFORM_DATA_CAPACITY + 1u> oversized{};
    CHECK(!termin::set_render_item_inline_uniform(
        item,
        "too_large",
        oversized.data(),
        static_cast<uint32_t>(oversized.size())));
}
