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

struct CustomGeometryPassDescriptor {
    std::string phase_mark;
    termin::MaterialPipelinePassContract shader_contract;
};

termin::VertexTransformContract material_static_transform()
{
    return termin::material_pipeline_make_static_mesh_vertex_transform_provider(
        "static",
        termin::MeshVertexTransformProfile::Material,
        "draw_data.u_model");
}

termin::VertexTransformContract compact_static_transform(const char* draw_resource)
{
    return termin::material_pipeline_make_static_mesh_vertex_transform_provider(
        "static_compact",
        termin::MeshVertexTransformProfile::Position,
        std::string(draw_resource) + ".u_model");
}

termin::VertexTransformContract normal_static_transform()
{
    return termin::material_pipeline_make_static_mesh_vertex_transform_provider(
        "static_position_normal",
        termin::MeshVertexTransformProfile::PositionNormal,
        "normal_draw.u_model");
}

termin::VertexTransformContract foliage_material_transform()
{
    return termin::material_pipeline_make_foliage_material_vertex_transform_provider(
        "foliage");
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

TEST_CASE("Skinned material provider is modular and owns deformation resources") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_make_skinned_mesh_vertex_transform_provider(
            "skinned",
            termin::MeshVertexTransformProfile::Material,
            "draw_data.u_model");

    CHECK(!contract.template_uuid.has_value());
    CHECK(termin::vertex_transform_provider_is_modular(contract));
    CHECK_EQ(contract.source_module.module_name, std::string("termin_vertex_transform"));
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

TEST_CASE("Skinned compact provider keeps position-only mesh input") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_make_skinned_mesh_vertex_transform_provider(
            "skinned_shadow",
            termin::MeshVertexTransformProfile::Position,
            "shadow_draw.u_model");

    CHECK(!contract.template_uuid.has_value());
    CHECK(termin::vertex_transform_provider_is_modular(contract));
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "joints", termin::MaterialPipelineValueType::Float4));
    CHECK(has_attribute(contract.vertex_inputs, "weights", termin::MaterialPipelineValueType::Float4));
    CHECK(!has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
}

TEST_CASE("Foliage material provider is modular and declares instanced resources") {
    termin::VertexTransformContract contract = foliage_material_transform();

    CHECK(!contract.template_uuid.has_value());
    CHECK(termin::vertex_transform_provider_is_modular(contract));
    CHECK_EQ(
        contract.source_module.module_name,
        std::string("termin_foliage_material_transform"));
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

TEST_CASE("Foliage auxiliary providers expose only the pass-required mesh ABI") {
    termin::VertexTransformContract position =
        termin::material_pipeline_make_foliage_vertex_transform_provider(
            "foliage_position",
            termin::MeshVertexTransformProfile::Position);
    CHECK(has_attribute(position.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(position.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(position.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
    CHECK(position.adapter_input_expression.find("instance_id") != std::string::npos);

    termin::VertexTransformContract position_normal =
        termin::material_pipeline_make_foliage_vertex_transform_provider(
            "foliage_position_normal",
            termin::MeshVertexTransformProfile::PositionNormal);
    CHECK(has_attribute(position_normal.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(position_normal.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(position_normal.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
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
    CHECK(termin::vertex_transform_provider_is_modular(contract));
    CHECK(contract.resources.empty());
}

TEST_CASE("Position-normal transform is data, not pass kind") {
    termin::VertexTransformContract contract = normal_static_transform();

    CHECK_EQ(contract.debug_name, std::string("static_position_normal"));
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(contract.adapter_input_expression.find("normal_draw.u_model") !=
          std::string::npos);
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
    CHECK(auxiliary_pass.static_vertex_transform->adapter_input_expression.find(
              "custom_draw.u_model") != std::string::npos);
}

TEST_CASE("Custom geometry pass labels do not select vertex contracts") {
    CustomGeometryPassDescriptor material_pass{
        "actor_attribute",
        termin::MaterialPipelinePassContract{}};
    material_pass.shader_contract.debug_name = "actor_attribute_material";
    material_pass.shader_contract.required_material_fragment_input =
        termin::material_pipeline_standard_material_fragment_interface();
    material_pass.shader_contract.static_vertex_transform = material_static_transform();

    CustomGeometryPassDescriptor compact_pass{
        "actor_attribute",
        termin::MaterialPipelinePassContract{}};
    compact_pass.shader_contract.debug_name = "actor_attribute_compact";
    compact_pass.shader_contract.required_material_fragment_input =
        termin::MaterialFragmentInterface{};
    compact_pass.shader_contract.static_vertex_transform =
        compact_static_transform("actor_attribute_draw");

    CHECK_EQ(material_pass.phase_mark, compact_pass.phase_mark);
    CHECK(termin::material_pipeline_interface_produces(
        material_pass.shader_contract.required_material_fragment_input,
        "world_pos",
        termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(
        material_pass.shader_contract.static_vertex_transform->vertex_inputs,
        "normal",
        termin::MaterialPipelineValueType::Float3));
    CHECK(compact_pass.shader_contract.required_material_fragment_input.semantics.empty());
    CHECK(has_attribute(
        compact_pass.shader_contract.static_vertex_transform->vertex_inputs,
        "position",
        termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(
        compact_pass.shader_contract.static_vertex_transform->vertex_inputs,
        "normal",
        termin::MaterialPipelineValueType::Float3));
    CHECK(compact_pass.shader_contract.static_vertex_transform->adapter_input_expression.find(
              "actor_attribute_draw.u_model") != std::string::npos);
}

TEST_CASE("Pass vertex resources remain pass-owned") {
    std::vector<termin::MaterialPipelineResourceDecl> resources =
        termin::material_pipeline_pass_vertex_resources("normal_draw");
    REQUIRE_EQ(resources.size(), 2u);
    CHECK(resources[0].owner == termin::MaterialPipelineResourceOwner::Pass);
    CHECK(resources[1].owner == termin::MaterialPipelineResourceOwner::Pass);
    CHECK_EQ(resources[0].requirement.name, std::string(TC_SHADER_RESOURCE_PER_FRAME));
    CHECK_EQ(resources[1].requirement.name, std::string("normal_draw"));
}
