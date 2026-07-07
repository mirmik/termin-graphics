#include "guard_main.h"

GUARD_TEST_MAIN();

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

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

constexpr const char* kStandardNormalFragmentSource = R"(
struct FragmentInput
{
    float4 screen_pos : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
};

struct FragmentOutput { float4 color : SV_Target0; };

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input)
{
    FragmentOutput output;
    float3 n = normalize(input.normal_world);
    output.color = float4(n * 0.5 + 0.5, 1.0);
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

const tc_shader_resource_requirement* contract_resource(
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
    termin::TcShaderCreateInfo create_info{};
    create_info.sources.vertex = "";
    create_info.sources.fragment = kFragmentSource;
    create_info.sources.name = "assembler-material-fragment";
    create_info.sources.fragment_entry = "fs_main";
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    termin::TcShader shader = termin::TcShader::from_sources(create_info);
    REQUIRE(shader.is_valid());
    tc_shader_set_feature(shader.get(), TC_SHADER_FEATURE_LIGHTING_UBO);

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

termin::MaterialPipelineMaterialContract material_contract_from_fragment(
    const char* fragment_source,
    const char* name)
{
    termin::TcShaderCreateInfo create_info{};
    create_info.sources.vertex = "";
    create_info.sources.fragment = fragment_source;
    create_info.sources.name = name;
    create_info.sources.fragment_entry = "fs_main";
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    termin::TcShader shader = termin::TcShader::from_sources(create_info);
    REQUIRE(shader.is_valid());
    return termin::material_pipeline_material_contract_from_shader(
        shader,
        termin::material_pipeline_standard_material_fragment_interface());
}

termin::MaterialPipelinePassContract material_pass_contract()
{
    termin::MaterialPipelinePassContract contract;
    contract.debug_name = "assembler_material_pass";
    contract.required_material_fragment_input =
        termin::material_pipeline_standard_material_fragment_interface();
    contract.uses_material_fragment = true;
    contract.static_vertex_transform =
        termin::material_pipeline_make_static_vertex_transform_contract(
            "static",
            termin::material_pipeline_full_material_mesh_input(),
            termin::material_pipeline_standard_material_fragment_interface(),
            termin::material_pipeline_common_vertex_resources("draw_data"));
    contract.skinned_vertex_transform =
        termin::material_pipeline_make_skinned_vertex_transform_contract(
            *contract.static_vertex_transform,
            "skinned",
            "termin-engine-skinned-material",
            termin::material_pipeline_skinned_material_mesh_input());
    contract.foliage_vertex_transform =
        termin::material_pipeline_make_foliage_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            "foliage",
            "termin-engine-foliage-instanced",
            termin::material_pipeline_foliage_material_mesh_input(),
            termin::material_pipeline_standard_material_fragment_interface(),
            termin::material_pipeline_foliage_vertex_resources());
    return contract;
}

termin::MaterialPipelinePassContract compact_auxiliary_pass_contract()
{
    termin::MaterialPipelinePassContract contract;
    contract.debug_name = "assembler_compact_auxiliary_pass";
    contract.required_material_fragment_input = termin::MaterialFragmentInterface{};
    contract.uses_material_fragment = true;
    contract.static_vertex_transform =
        termin::material_pipeline_make_static_vertex_transform_contract(
            "static_compact",
            termin::material_pipeline_position_mesh_input(),
            termin::material_pipeline_standard_material_fragment_interface(),
            termin::material_pipeline_common_vertex_resources("compact_draw"));
    contract.skinned_vertex_transform =
        termin::material_pipeline_make_skinned_vertex_transform_contract(
            *contract.static_vertex_transform,
            "skinned_compact",
            "termin-engine-skinned-shadow",
            termin::material_pipeline_skinned_position_mesh_input());
    return contract;
}

} // namespace

TEST_CASE("material pipeline assembler attaches skinned shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-skinned-contract";
    request.shader_uuid = "assembler-skinned-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    REQUIRE(tc_shader_has_feature(result.shader.get(), TC_SHADER_FEATURE_LIGHTING_UBO));
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
    CHECK_EQ(view.source_kind, TC_SHADER_CONTRACT_SOURCE_ASSEMBLED);
    CHECK(contract_has_vertex_input(view, "position"));
    CHECK(contract_has_vertex_input(view, "joints"));
    CHECK(contract_has_vertex_input(view, "weights"));

    const tc_shader_resource_requirement* bone =
        contract_resource(view, TC_SHADER_RESOURCE_BONE_BLOCK);
    REQUIRE(bone != nullptr);
    CHECK_EQ(bone->scope, TC_SHADER_RESOURCE_SCOPE_DRAW);
    CHECK(!tc_shader_has_resource_layout(result.shader.get()));
    CHECK(tc_shader_find_resource_binding(
              result.shader.get(),
              TC_SHADER_RESOURCE_BONE_BLOCK) == nullptr);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material pipeline assembler keeps skinned debug normal material semantics linkable") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract_from_fragment(
        kStandardNormalFragmentSource,
        "assembler-standard-normal-fragment");
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-skinned-standard-normal";
    request.shader_uuid = "assembler-skinned-standard-normal";

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    REQUIRE(result.shader.get() != nullptr);
    const std::string vertex_source = result.shader.vertex_source();
    const std::string fragment_source = result.shader.fragment_source();
    CHECK(vertex_source.find("normal_world : TEXCOORD1") != std::string::npos);
    CHECK(fragment_source.find("normal_world : TEXCOORD1") != std::string::npos);
    CHECK(fragment_source.find("normal_world : NORMAL") == std::string::npos);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material pipeline assembler attaches foliage instance contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.foliage_vertex_transform;
    request.shader_name = "assembler-foliage-contract";
    request.shader_uuid = "assembler-foliage-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
    CHECK(contract_has_vertex_input(view, "position"));
    CHECK(contract_has_vertex_input(view, "normal"));
    CHECK(contract_has_vertex_input(view, "uv"));

    const tc_shader_resource_requirement* instances =
        contract_resource(view, "foliage_instances");
    REQUIRE(instances != nullptr);
    CHECK_EQ(instances->kind, TC_SHADER_RESOURCE_STORAGE_BUFFER);
    CHECK_EQ(instances->element_stride, 32u);
    CHECK(!tc_shader_has_resource_layout(result.shader.get()));
    CHECK(tc_shader_find_resource_binding(
              result.shader.get(),
              "foliage_instances") == nullptr);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material mesh input selection follows static compact shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = compact_auxiliary_pass_contract();
    request.vertex_transform = *request.pass.static_vertex_transform;
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
    request.pass = compact_auxiliary_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
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

TEST_CASE("material mesh input selection keeps full skinned material attributes") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-skinned-full-material-contract";
    request.shader_uuid = "assembler-skinned-full-material-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    CHECK(
        termin::material_mesh_vertex_input_for_shader(
            result.shader.get(),
            termin::MaterialMeshVertexInput::FullMaterial) ==
        termin::MaterialMeshVertexInput::SkinnedFullMaterial);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material shader intent fingerprint includes skinned vertex transform") {
    tc_shader_init();

    termin::MaterialPipelineMaterialContract material = material_contract();
    termin::MaterialPipelinePassContract pass_a = compact_auxiliary_pass_contract();
    termin::MaterialPipelinePassContract pass_b = pass_a;
    pass_b.skinned_vertex_transform =
        termin::material_pipeline_make_skinned_vertex_transform_contract(
            *pass_b.static_vertex_transform,
            "skinned_compact",
            "termin-engine-skinned-depth",
            termin::material_pipeline_skinned_position_mesh_input());

    const std::string fingerprint_a =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_SKINNING,
            *pass_a.skinned_vertex_transform,
            pass_a);
    const std::string fingerprint_b =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_SKINNING,
            *pass_b.skinned_vertex_transform,
            pass_b);

    CHECK(fingerprint_a != fingerprint_b);

    tc_shader_destroy(material.shader.handle);
    tc_shader_shutdown();
}
