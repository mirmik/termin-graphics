#include "guard_main.h"

GUARD_TEST_MAIN();

#include <algorithm>
#include <string>

#include <termin/render/material_pipeline_variant.hpp>

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
