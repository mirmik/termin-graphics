#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/render_item_submission.hpp>
#include <termin/render/render_task.hpp>

#include <array>
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
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Color));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Shadow));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Id));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Depth));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::DepthOnly));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Normal));
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
    desc.capabilities.pass_semantic_mask =
        termin::render_item_pass_semantic_bit(termin::RenderItemPassSemantic::Color)
        | termin::render_item_pass_semantic_bit(termin::RenderItemPassSemantic::Shadow);
    desc.capabilities.requires_draw_context = true;
    desc.capabilities.consumes_common_resources = false;
    desc.capabilities.supported_task_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
    desc.capabilities.required_task_input_mask =
        termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);

    REQUIRE(termin::register_render_item_draw_encoder(test_kind, desc));

    termin::RenderItemEncoderCapabilities capabilities{};
    REQUIRE(termin::get_render_item_encoder_capabilities(test_kind, capabilities));
    CHECK(termin::render_item_encoder_supports_pass(
        test_kind,
        termin::RenderItemPassSemantic::Color));
    CHECK(termin::render_item_encoder_supports_pass(
        test_kind,
        termin::RenderItemPassSemantic::Shadow));
    CHECK(!termin::render_item_encoder_supports_pass(
        test_kind,
        termin::RenderItemPassSemantic::Depth));
    CHECK(capabilities.requires_draw_context);
    CHECK(!capabilities.consumes_common_resources);

    CHECK(termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
    CHECK(!termin::get_render_item_encoder_capabilities(test_kind, capabilities));
}

TEST_CASE("RenderItem task planner accepts a supported mesh contract") {
    tc_render_item item{};
    item.kind = TC_RENDER_ITEM_KIND_MESH;
    item.flags = TC_RENDER_ITEM_FLAG_HAS_MODEL_MATRIX;
    tc_material_phase phase{};

    termin::MaterialPipelinePassContract shader_contract{};
    termin::RenderItemTaskPlanningContract contract{};
    contract.pass_semantic = termin::RenderItemPassSemantic::Color;
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
    request.candidate_shader.index = 1u;
    request.candidate_shader.generation = 1u;
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
    CHECK(task.pass_semantic == termin::RenderItemPassSemantic::Color);
    CHECK(task.has_vertex_transform_kind);
    CHECK(task.vertex_transform_kind == termin::VertexTransformKind::StaticMesh);
}

TEST_CASE("RenderItem task planner delegates final shader selection to the encoder hook") {
    constexpr uint32_t test_kind = 0x7fff0004u;
    termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr);

    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.plan_task_shader = selecting_test_shader_planner;
    desc.debug_name = "selecting_test_encoder";
    desc.capabilities.pass_semantic_mask =
        termin::render_item_pass_semantic_bit(termin::RenderItemPassSemantic::Color);
    REQUIRE(termin::register_render_item_draw_encoder(test_kind, desc));

    tc_render_item item{};
    item.kind = test_kind;
    termin::MaterialPipelinePassContract shader_contract{};
    termin::RenderItemTaskPlanningContract contract{};
    contract.pass_semantic = termin::RenderItemPassSemantic::Color;
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

TEST_CASE("RenderItem task planner rejects material input transform and output mismatches") {
    tc_render_item item{};
    item.kind = TC_RENDER_ITEM_KIND_MESH;
    item.flags = TC_RENDER_ITEM_FLAG_HAS_SKINNING_MATRICES;

    termin::RenderItemTaskPlanningContract contract{};
    contract.pass_semantic = termin::RenderItemPassSemantic::Color;
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
    contract.pass_semantic = static_cast<termin::RenderItemPassSemantic>(63u);
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
