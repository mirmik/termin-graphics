#include "guard_main.h"

GUARD_TEST_MAIN();

#include <algorithm>
#include <string>

#include <termin/render/material_pipeline_shader_assembler.hpp>
#include <termin/render/vertex_transform_contracts.hpp>

namespace {

bool has_attribute(
    const termin::VertexInputContract& input,
    const std::string& name,
    termin::MaterialPipelineValueType type)
{
    return std::any_of(
        input.mesh_attributes.begin(),
        input.mesh_attributes.end(),
        [&](const termin::MaterialPipelineSemantic& semantic) {
            return semantic.name == name && semantic.type == type;
        });
}

const termin::MaterialPipelineResourceDecl* find_resource(
    const termin::VertexTransformContract& contract,
    const std::string& name)
{
    auto it = std::find_if(
        contract.resources.begin(),
        contract.resources.end(),
        [&](const termin::MaterialPipelineResourceDecl& resource) {
            return resource.requirement.name == name;
        });
    return it == contract.resources.end() ? nullptr : &(*it);
}

termin::VertexTransformContract material_static_transform()
{
    return termin::material_pipeline_make_static_vertex_transform_contract(
        "static",
        termin::material_pipeline_full_material_mesh_input(),
        termin::material_pipeline_standard_material_fragment_interface(),
        termin::material_pipeline_common_vertex_resources("draw_data"));
}

termin::VertexTransformContract compact_static_transform(const char* draw_resource)
{
    return termin::material_pipeline_make_static_vertex_transform_contract(
        "static_compact",
        termin::material_pipeline_position_mesh_input(),
        termin::material_pipeline_standard_material_fragment_interface(),
        termin::material_pipeline_common_vertex_resources(draw_resource));
}

termin::VertexTransformContract normal_static_transform()
{
    return termin::material_pipeline_make_static_vertex_transform_contract(
        "static_position_normal",
        termin::material_pipeline_position_normal_mesh_input(),
        termin::material_pipeline_standard_material_fragment_interface(),
        termin::material_pipeline_common_vertex_resources("normal_draw"));
}

termin::VertexTransformContract foliage_material_transform()
{
    return termin::material_pipeline_make_foliage_vertex_transform_contract(
        termin::VertexTransformKind::Foliage,
        "foliage",
        "termin-engine-foliage-instanced",
        termin::material_pipeline_foliage_material_mesh_input(),
        termin::material_pipeline_standard_material_fragment_interface(),
        termin::material_pipeline_foliage_vertex_resources());
}

termin::VertexTransformContract foliage_shadow_transform()
{
    return termin::material_pipeline_make_foliage_vertex_transform_contract(
        termin::VertexTransformKind::FoliageShadow,
        "foliage_shadow",
        "termin-engine-foliage-shadow",
        termin::material_pipeline_position_mesh_input(),
        termin::material_pipeline_standard_material_fragment_interface(),
        termin::material_pipeline_foliage_vertex_resources());
}

} // namespace

TEST_CASE("Skinned transform builder declares template, inputs, and bone block") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_make_skinned_vertex_transform_contract(
            material_static_transform(),
            "skinned",
            "termin-engine-skinned-material",
            termin::material_pipeline_skinned_material_mesh_input());

    REQUIRE(contract.template_uuid.has_value());
    CHECK_EQ(*contract.template_uuid, std::string("termin-engine-skinned-material"));
    CHECK_EQ(contract.vertex_entry, std::string("vs_main"));

    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
    CHECK(has_attribute(contract.vertex_inputs, "tangent", termin::MaterialPipelineValueType::Float4));
    CHECK(has_attribute(contract.vertex_inputs, "joints", termin::MaterialPipelineValueType::Float4));
    CHECK(has_attribute(contract.vertex_inputs, "weights", termin::MaterialPipelineValueType::Float4));

    const termin::MaterialPipelineResourceDecl* bone_block =
        find_resource(contract, TC_SHADER_RESOURCE_BONE_BLOCK);
    REQUIRE(bone_block != nullptr);
    CHECK_EQ(bone_block->requirement.scope, static_cast<uint32_t>(TC_SHADER_RESOURCE_SCOPE_DRAW));
    CHECK(bone_block->owner == termin::MaterialPipelineResourceOwner::VertexTransform);

    CHECK(termin::material_pipeline_interface_produces(
        contract.produced_fragment_input,
        "world_pos",
        termin::MaterialPipelineValueType::Float3));
}

TEST_CASE("Skinned compact transform uses position skinning input") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_make_skinned_vertex_transform_contract(
            compact_static_transform("shadow_draw"),
            "skinned_shadow",
            "termin-engine-skinned-shadow",
            termin::material_pipeline_skinned_position_mesh_input());

    REQUIRE(contract.template_uuid.has_value());
    CHECK_EQ(*contract.template_uuid, std::string("termin-engine-skinned-shadow"));
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "joints", termin::MaterialPipelineValueType::Float4));
    CHECK(has_attribute(contract.vertex_inputs, "weights", termin::MaterialPipelineValueType::Float4));
    CHECK(!has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
}

TEST_CASE("Foliage transform builder declares instanced resources") {
    termin::VertexTransformContract contract = foliage_material_transform();

    REQUIRE(contract.template_uuid.has_value());
    CHECK_EQ(*contract.template_uuid, std::string("termin-engine-foliage-instanced"));
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));

    const termin::MaterialPipelineResourceDecl* foliage_draw =
        find_resource(contract, "foliage_draw");
    REQUIRE(foliage_draw != nullptr);
    CHECK_EQ(foliage_draw->requirement.scope, static_cast<uint32_t>(TC_SHADER_RESOURCE_SCOPE_DRAW));
    CHECK_EQ(foliage_draw->requirement.kind, static_cast<uint32_t>(TC_SHADER_RESOURCE_CONSTANT_BUFFER));
    CHECK(foliage_draw->owner == termin::MaterialPipelineResourceOwner::VertexTransform);

    const termin::MaterialPipelineResourceDecl* foliage_instances =
        find_resource(contract, "foliage_instances");
    REQUIRE(foliage_instances != nullptr);
    CHECK_EQ(foliage_instances->requirement.kind, static_cast<uint32_t>(TC_SHADER_RESOURCE_STORAGE_BUFFER));
    CHECK(foliage_instances->owner == termin::MaterialPipelineResourceOwner::VertexTransform);

    REQUIRE_EQ(contract.instance_streams.size(), 1u);
    CHECK_EQ(contract.instance_streams[0].name, std::string("foliage_instances"));
}

TEST_CASE("Foliage shadow transform uses position-only mesh input") {
    termin::VertexTransformContract contract = foliage_shadow_transform();

    REQUIRE(contract.template_uuid.has_value());
    CHECK_EQ(*contract.template_uuid, std::string("termin-engine-foliage-shadow"));
    CHECK(contract.kind == termin::VertexTransformKind::FoliageShadow);
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
}

TEST_CASE("Static transform is descriptor-built") {
    termin::VertexTransformContract contract = material_static_transform();

    CHECK(!contract.template_uuid.has_value());
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));

    const termin::MaterialPipelineResourceDecl* draw_data =
        find_resource(contract, "draw_data");
    REQUIRE(draw_data != nullptr);
    CHECK(draw_data->owner == termin::MaterialPipelineResourceOwner::VertexTransform);
}

TEST_CASE("Position-normal transform is data, not pass kind") {
    termin::VertexTransformContract contract = normal_static_transform();

    CHECK_EQ(contract.debug_name, std::string("static_position_normal"));
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(find_resource(contract, "normal_draw") != nullptr);
}

TEST_CASE("Pass contracts carry fragment input intent explicitly") {
    termin::MaterialPipelinePassContract material_pass;
    material_pass.debug_name = "arbitrary_material_pass";
    material_pass.required_material_fragment_input =
        termin::material_pipeline_standard_material_fragment_interface();
    material_pass.static_vertex_transform = material_static_transform();

    CHECK(termin::material_pipeline_interface_produces(
        material_pass.required_material_fragment_input,
        "world_pos",
        termin::MaterialPipelineValueType::Float3));
    CHECK(termin::material_pipeline_interface_produces(
        material_pass.required_material_fragment_input,
        "normal_world",
        termin::MaterialPipelineValueType::Float3));

    termin::MaterialPipelinePassContract auxiliary_pass;
    auxiliary_pass.debug_name = "arbitrary_auxiliary_pass";
    auxiliary_pass.required_material_fragment_input = termin::MaterialFragmentInterface{};
    auxiliary_pass.static_vertex_transform = compact_static_transform("custom_draw");
    CHECK(auxiliary_pass.required_material_fragment_input.semantics.empty());
    CHECK(find_resource(*auxiliary_pass.static_vertex_transform, "custom_draw") != nullptr);
}
