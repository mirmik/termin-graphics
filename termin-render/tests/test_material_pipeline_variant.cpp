#include "guard_main.h"

GUARD_TEST_MAIN();

#include <algorithm>
#include <cstring>
#include <string>

#include <termin/render/material_pipeline.hpp>
#include <termin/render/material_pipeline_variant.hpp>

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace {

termin::MaterialPipelineSemantic semantic(
    const char* name,
    termin::MaterialPipelineValueType type)
{
    return {name, type};
}

termin::MaterialContract standard_material_contract()
{
    termin::MaterialContract material;
    material.debug_name = "standard";
    material.fragment_source =
        "struct FragmentInput {\n"
        "    float3 world_pos : TEXCOORD0;\n"
        "    float3 normal_world : TEXCOORD1;\n"
        "    float2 uv : TEXCOORD2;\n"
        "};\n"
        "[shader(\"fragment\")]\n"
        "float4 fs_main(FragmentInput input) : SV_Target {\n"
        "    return float4(input.uv, 0.0, 1.0);\n"
        "}\n";
    material.fragment_entry = "fs_main";
    material.fragment_input.semantics = {
        semantic("world_pos", termin::MaterialPipelineValueType::Float3),
        semantic("normal_world", termin::MaterialPipelineValueType::Float3),
        semantic("uv", termin::MaterialPipelineValueType::Float2),
    };

    termin::MaterialPipelineResourceDecl material_ubo{};
    material_ubo.name = TC_SHADER_RESOURCE_MATERIAL;
    material_ubo.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    material_ubo.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    material_ubo.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    material_ubo.binding = 1;
    material_ubo.has_placement = true;
    material_ubo.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    material_ubo.owner = termin::MaterialPipelineResourceOwner::Material;
    material.resources.push_back(material_ubo);
    return material;
}

const termin::MaterialPipelineResourceDecl* find_resource(
    const termin::MaterialPipelineVariantPlan& plan,
    const std::string& name)
{
    auto it = std::find_if(
        plan.resources.begin(),
        plan.resources.end(),
        [&](const termin::MaterialPipelineResourceDecl& resource) {
            return resource.name == name;
        });
    return it == plan.resources.end() ? nullptr : &(*it);
}

const termin::MaterialPipelineResourceDecl* find_resource(
    const termin::MaterialContract& material,
    const std::string& name)
{
    auto it = std::find_if(
        material.resources.begin(),
        material.resources.end(),
        [&](const termin::MaterialPipelineResourceDecl& resource) {
            return resource.name == name;
        });
    return it == material.resources.end() ? nullptr : &(*it);
}

bool has_diagnostic(
    const termin::MaterialPipelineVariantPlan& plan,
    termin::MaterialPipelineDiagnosticCode code)
{
    return std::any_of(
        plan.diagnostics.begin(),
        plan.diagnostics.end(),
        [&](const termin::MaterialPipelineDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

} // namespace

TEST_CASE("Material pipeline planner accepts foliage color material contract") {
    termin::MaterialPipelineVariantRequest request{};
    request.material = standard_material_contract();
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            termin::MaterialPipelinePassKind::Color);
    request.pass =
        termin::material_pipeline_builtin_pass_contract(
            termin::MaterialPipelinePassKind::Color);

    termin::MaterialPipelineVariantPlan plan =
        termin::material_pipeline_plan_variant(request);

    REQUIRE(plan.ok());
    CHECK(find_resource(plan, TC_SHADER_RESOURCE_MATERIAL) != nullptr);
    CHECK(find_resource(plan, "foliage_draw") != nullptr);
    CHECK(find_resource(plan, "foliage_instances") != nullptr);
}

TEST_CASE("Material pipeline planner rejects unsupported material fragment input") {
    termin::MaterialPipelineVariantRequest request{};
    request.material = standard_material_contract();
    request.material.fragment_input.semantics.push_back(
        semantic("custom_color", termin::MaterialPipelineValueType::Float4));
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            termin::MaterialPipelinePassKind::Color);
    request.pass =
        termin::material_pipeline_builtin_pass_contract(
            termin::MaterialPipelinePassKind::Color);

    termin::MaterialPipelineVariantPlan plan =
        termin::material_pipeline_plan_variant(request);

    REQUIRE(!plan.ok());
    CHECK(has_diagnostic(
        plan,
        termin::MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic));
    CHECK(plan.diagnostics[0].message.find("custom_color") != std::string::npos);
}

TEST_CASE("Material pipeline planner reports resource placement conflicts") {
    termin::MaterialPipelineVariantRequest request{};
    request.material = standard_material_contract();
    termin::MaterialPipelineResourceDecl bad_draw{};
    bad_draw.name = "draw_data";
    bad_draw.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bad_draw.scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    bad_draw.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    bad_draw.binding = 24;
    bad_draw.has_placement = true;
    bad_draw.stage_mask = TC_SHADER_STAGE_VERTEX;
    bad_draw.owner = termin::MaterialPipelineResourceOwner::Material;
    request.material.resources.push_back(bad_draw);

    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            termin::MaterialPipelinePassKind::Color);
    request.pass =
        termin::material_pipeline_builtin_pass_contract(
            termin::MaterialPipelinePassKind::Color);

    termin::MaterialPipelineVariantPlan plan =
        termin::material_pipeline_plan_variant(request);

    REQUIRE(!plan.ok());
    CHECK(has_diagnostic(
        plan,
        termin::MaterialPipelineDiagnosticCode::ResourcePlacementConflict));
}

TEST_CASE("Material pipeline planner uses pass fragment contract when material fragment is overridden") {
    termin::MaterialPipelineVariantRequest request{};
    request.material = standard_material_contract();
    request.material.fragment_input.semantics.push_back(
        semantic("custom_color", termin::MaterialPipelineValueType::Float4));
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::FoliageShadow,
            termin::MaterialPipelinePassKind::Shadow);
    request.pass =
        termin::material_pipeline_builtin_pass_contract(
            termin::MaterialPipelinePassKind::Shadow);

    termin::MaterialPipelineVariantPlan plan =
        termin::material_pipeline_plan_variant(request);

    CHECK(plan.ok());
    CHECK(find_resource(plan, "foliage_draw") != nullptr);
    CHECK(find_resource(plan, "foliage_instances") != nullptr);
}

TEST_CASE("Material pipeline creates foliage color shader from contract sources") {
    tc_shader_init();

    termin::MaterialPipelineVariantRequest request{};
    request.material = standard_material_contract();
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            termin::MaterialPipelinePassKind::Color);
    request.pass =
        termin::material_pipeline_builtin_pass_contract(
            termin::MaterialPipelinePassKind::Color);
    request.shader_name = "contract-foliage-color";
    request.shader_uuid = "contract-foliage-color-variant";

    termin::MaterialPipelineCompiledVariant variant =
        termin::material_pipeline_create_variant(request);

    REQUIRE(variant.ok());
    REQUIRE(variant.shader.is_valid());
    CHECK(std::string(variant.shader.name()) == "contract-foliage-color");
    CHECK(std::string(variant.shader.vertex_source()).find("foliage_instances") != std::string::npos);
    CHECK(std::string(variant.shader.fragment_source()).find("float4 fs_main") != std::string::npos);

    const tc_shader* raw = variant.shader.get();
    REQUIRE(raw != nullptr);
    CHECK(tc_shader_find_resource_binding(raw, TC_SHADER_RESOURCE_MATERIAL) != nullptr);
    CHECK(tc_shader_find_resource_binding(raw, "foliage_draw") != nullptr);
    CHECK(tc_shader_find_resource_binding(raw, "foliage_instances") != nullptr);

    tc_shader_shutdown();
}

TEST_CASE("Material pipeline creates pass-fragment shader when pass overrides material fragment") {
    tc_shader_init();

    termin::MaterialPipelineVariantRequest request{};
    request.material = standard_material_contract();
    request.material.fragment_input.semantics.push_back(
        semantic("custom_color", termin::MaterialPipelineValueType::Float4));
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::FoliageShadow,
            termin::MaterialPipelinePassKind::Shadow);
    request.pass =
        termin::material_pipeline_builtin_pass_contract(
            termin::MaterialPipelinePassKind::Shadow);
    request.pass.fragment_source_override =
        "[shader(\"fragment\")]\n"
        "void fs_main() {}\n";
    request.pass.fragment_entry_override = "fs_main";
    request.shader_name = "contract-foliage-shadow";
    request.shader_uuid = "contract-foliage-shadow-variant";

    termin::MaterialPipelineCompiledVariant variant =
        termin::material_pipeline_create_variant(request);

    REQUIRE(variant.ok());
    REQUIRE(variant.shader.is_valid());
    CHECK(std::string(variant.shader.vertex_source()).find("foliage_instances") != std::string::npos);
    CHECK(std::string(variant.shader.fragment_source()).find("void fs_main") != std::string::npos);
    CHECK(std::string(variant.shader.fragment_source()).find("custom_color") == std::string::npos);

    const tc_shader* raw = variant.shader.get();
    REQUIRE(raw != nullptr);
    CHECK(tc_shader_find_resource_binding(raw, "foliage_draw") != nullptr);
    CHECK(tc_shader_find_resource_binding(raw, "foliage_instances") != nullptr);

    tc_shader_shutdown();
}

TEST_CASE("Material contract extraction keeps material resources and drops old draw resources") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_from_sources_with_entries_ex(
        "void vs_main() {}\n",
        standard_material_contract().fragment_source.c_str(),
        nullptr,
        "legacy-contract-source",
        nullptr,
        "legacy-contract-source-uuid",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED,
        "vs_main",
        "fs_main",
        nullptr);
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* raw = tc_shader_get(handle);
    REQUIRE(raw != nullptr);

    tc_shader_resource_binding bindings[3]{};
    std::strncpy(bindings[0].name, TC_SHADER_RESOURCE_MATERIAL, TC_SHADER_RESOURCE_NAME_MAX - 1);
    bindings[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    bindings[0].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    bindings[0].binding = 1;
    bindings[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;

    std::strncpy(bindings[1].name, "draw_data", TC_SHADER_RESOURCE_NAME_MAX - 1);
    bindings[1].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[1].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    bindings[1].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    bindings[1].binding = 24;
    bindings[1].stage_mask = TC_SHADER_STAGE_VERTEX;

    std::strncpy(bindings[2].name, "lighting", TC_SHADER_RESOURCE_NAME_MAX - 1);
    bindings[2].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[2].scope = TC_SHADER_RESOURCE_SCOPE_PASS;
    bindings[2].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    bindings[2].binding = 0;
    bindings[2].stage_mask = TC_SHADER_STAGE_FRAGMENT;
    tc_shader_set_resource_layout(raw, bindings, 3);

    termin::MaterialFragmentInterface fragment_input;
    fragment_input.semantics = {
        semantic("world_pos", termin::MaterialPipelineValueType::Float3),
    };

    termin::MaterialContract material =
        termin::material_pipeline_material_contract_from_shader(
            termin::TcShader(handle),
            fragment_input);

    REQUIRE(find_resource(material, TC_SHADER_RESOURCE_MATERIAL) != nullptr);
    CHECK(find_resource(material, "lighting") != nullptr);
    CHECK(find_resource(material, "draw_data") == nullptr);

    termin::MaterialPipelineVariantRequest request{};
    request.material = material;
    request.vertex_transform =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            termin::MaterialPipelinePassKind::Color);
    request.pass =
        termin::material_pipeline_builtin_pass_contract(
            termin::MaterialPipelinePassKind::Color);

    termin::MaterialPipelineVariantPlan plan =
        termin::material_pipeline_plan_variant(request);
    CHECK(plan.ok());
    CHECK(find_resource(plan, TC_SHADER_RESOURCE_MATERIAL) != nullptr);
    CHECK(find_resource(plan, "lighting") != nullptr);
    CHECK(find_resource(plan, "foliage_draw") != nullptr);
    CHECK(find_resource(plan, "draw_data") == nullptr);

    tc_shader_shutdown();
}

TEST_CASE("Legacy material vertex variant bridge uses contract resource layout") {
    tc_shader_init();

    const termin::MaterialContract material_source = standard_material_contract();
    tc_shader_handle handle = tc_shader_from_sources_with_entries_ex(
        "void vs_main() {}\n",
        material_source.fragment_source.c_str(),
        nullptr,
        "legacy-foliage-source",
        nullptr,
        "legacy-foliage-source-uuid",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED,
        "vs_main",
        "fs_main",
        nullptr);
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* raw = tc_shader_get(handle);
    REQUIRE(raw != nullptr);

    tc_shader_resource_binding bindings[2]{};
    std::strncpy(bindings[0].name, TC_SHADER_RESOURCE_MATERIAL, TC_SHADER_RESOURCE_NAME_MAX - 1);
    bindings[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    bindings[0].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    bindings[0].binding = 1;
    bindings[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;

    std::strncpy(bindings[1].name, "draw_data", TC_SHADER_RESOURCE_NAME_MAX - 1);
    bindings[1].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[1].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    bindings[1].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    bindings[1].binding = 24;
    bindings[1].stage_mask = TC_SHADER_STAGE_VERTEX;
    tc_shader_set_resource_layout(raw, bindings, 2);

    termin::MaterialVertexVariantRequest request{};
    request.original_shader = termin::TcShader(handle);
    request.variant_op = TC_SHADER_VARIANT_FOLIAGE;
    request.vertex_template_uuid = "termin-engine-foliage-instanced";
    request.variant_name_suffix = "_Foliage";
    request.debug_context = "LegacyBridgeTest";
    request.vertex_entry = "vs_main";
    request.require_slang_original = true;

    termin::TcShader variant = termin::get_material_vertex_variant(request);

    REQUIRE(variant.is_valid());
    CHECK(variant.variant_op() == TC_SHADER_VARIANT_FOLIAGE);
    CHECK(std::string(variant.name()) == "legacy-foliage-source_Foliage");

    const tc_shader* variant_raw = variant.get();
    REQUIRE(variant_raw != nullptr);
    CHECK(tc_shader_find_resource_binding(variant_raw, TC_SHADER_RESOURCE_MATERIAL) != nullptr);
    CHECK(tc_shader_find_resource_binding(variant_raw, "foliage_draw") != nullptr);
    CHECK(tc_shader_find_resource_binding(variant_raw, "foliage_instances") != nullptr);
    CHECK(tc_shader_find_resource_binding(variant_raw, "draw_data") == nullptr);

    tc_shader_shutdown();
}
