#include "guard_main.h"

GUARD_TEST_MAIN();

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <termin/render/material_pipeline.hpp>
#include <termin/render/material_pipeline_shader_assembler.hpp>

namespace {

constexpr const char* kVertexSource = R"(
import termin_prelude;
struct VertexOutput { float4 position : SV_Position; };
[shader("vertex")]
VertexOutput vs_main() {
    VertexOutput output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
)";

constexpr const char* kFragmentSource = R"(
struct FragmentOutput { float4 color : SV_Target0; };
[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}
)";

bool contract_has_vertex_input(
    const tc_shader_contract_view& view,
    const char* semantic)
{
    for (uint32_t i = 0; i < view.vertex_input_count; ++i) {
        if (std::strcmp(view.vertex_inputs[i].semantic, semantic) == 0) {
            return true;
        }
    }
    return false;
}

const tc_shader_resource_binding* contract_resource(
    const tc_shader_contract_view& view,
    const char* name)
{
    for (uint32_t i = 0; i < view.resource_count; ++i) {
        if (std::strcmp(view.resources[i].name, name) == 0) {
            return &view.resources[i];
        }
    }
    return nullptr;
}

termin::MaterialPipelineMaterialContract material_contract()
{
    termin::TcShader shader = termin::TcShader::from_sources(
        "",
        kFragmentSource,
        "",
        "assembler-material-fragment",
        "",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED,
        "",
        "fs_main");
    REQUIRE(shader.is_valid());

    tc_shader_resource_binding resources[2]{};
    std::snprintf(resources[0].name, sizeof(resources[0].name), "%s", TC_SHADER_RESOURCE_MATERIAL);
    resources[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resources[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    resources[0].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    resources[0].binding = 1;
    resources[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;
    resources[0].size = 64;

    std::snprintf(resources[1].name, sizeof(resources[1].name), "%s", "draw_data");
    resources[1].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resources[1].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    resources[1].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    resources[1].binding = 24;
    resources[1].stage_mask = TC_SHADER_STAGE_VERTEX;
    resources[1].size = 64;

    tc_shader_set_resource_layout(shader.get(), resources, 2);

    return termin::material_pipeline_material_contract_from_shader(
        shader,
        termin::material_pipeline_standard_material_fragment_interface());
}

} // namespace

TEST_CASE("material pipeline assembler attaches skinned shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::SkinnedMesh,
            termin::MaterialPipelinePassKind::Color);
    request.pass = termin::material_pipeline_builtin_pass_contract(
        termin::MaterialPipelinePassKind::Color);
    request.shader_name = "assembler-skinned-contract";
    request.shader_uuid = "assembler-skinned-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
    CHECK_EQ(view.producer_kind, TC_SHADER_CONTRACT_PRODUCER_MATERIAL_PIPELINE);
    CHECK_EQ(view.draw_kind, TC_SHADER_CONTRACT_DRAW_MESH);
    CHECK(contract_has_vertex_input(view, "position"));
    CHECK(contract_has_vertex_input(view, "joints"));
    CHECK(contract_has_vertex_input(view, "weights"));

    const tc_shader_resource_binding* bone =
        contract_resource(view, TC_SHADER_RESOURCE_BONE_BLOCK);
    REQUIRE(bone != nullptr);
    CHECK_EQ(bone->scope, TC_SHADER_RESOURCE_SCOPE_DRAW);
    CHECK_EQ(bone->binding, 16u);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material pipeline assembler attaches foliage instance contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            termin::MaterialPipelinePassKind::Color);
    request.pass = termin::material_pipeline_builtin_pass_contract(
        termin::MaterialPipelinePassKind::Color);
    request.shader_name = "assembler-foliage-contract";
    request.shader_uuid = "assembler-foliage-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
    CHECK_EQ(view.draw_kind, TC_SHADER_CONTRACT_DRAW_INSTANCED_MESH);
    CHECK(contract_has_vertex_input(view, "position"));
    CHECK(contract_has_vertex_input(view, "normal"));
    CHECK(contract_has_vertex_input(view, "uv"));

    REQUIRE_EQ(view.storage_buffer_count, 1u);
    CHECK(std::strcmp(view.storage_buffers[0].resource_name, "foliage_instances") == 0);
    CHECK_EQ(view.storage_buffers[0].stride, 32u);

    const tc_shader_resource_binding* instances =
        contract_resource(view, "foliage_instances");
    REQUIRE(instances != nullptr);
    CHECK_EQ(instances->kind, TC_SHADER_RESOURCE_STORAGE_BUFFER);
    CHECK_EQ(instances->binding, 25u);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material mesh input selection follows static compact shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::StaticMesh,
            termin::MaterialPipelinePassKind::Shadow);
    request.pass = termin::material_pipeline_builtin_pass_contract(
        termin::MaterialPipelinePassKind::Shadow);
    request.shader_name = "assembler-static-shadow-contract";
    request.shader_uuid = "assembler-static-shadow-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    CHECK(
        termin::material_mesh_vertex_input_for_shader(
            result.shader.get(),
            termin::MaterialMeshVertexInput::FullMaterial) ==
        termin::MaterialMeshVertexInput::Position);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material mesh input selection follows skinned compact shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::SkinnedMesh,
            termin::MaterialPipelinePassKind::Shadow);
    request.pass = termin::material_pipeline_builtin_pass_contract(
        termin::MaterialPipelinePassKind::Shadow);
    request.shader_name = "assembler-skinned-shadow-contract";
    request.shader_uuid = "assembler-skinned-shadow-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    CHECK(
        termin::material_mesh_vertex_input_for_shader(
            result.shader.get(),
            termin::MaterialMeshVertexInput::FullMaterial) ==
        termin::MaterialMeshVertexInput::SkinnedPositionJointsWeights);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}
