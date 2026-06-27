#include "guard_main.h"

GUARD_TEST_MAIN();

#include <algorithm>
#include <string>

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

} // namespace

TEST_CASE("Skinned material transform declares template, inputs, and bone block") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::SkinnedMesh,
            termin::MaterialPipelinePassKind::Color);

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

TEST_CASE("Skinned shadow transform uses compact position skinning input") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::SkinnedMesh,
            termin::MaterialPipelinePassKind::Shadow);

    REQUIRE(contract.template_uuid.has_value());
    CHECK_EQ(*contract.template_uuid, std::string("termin-engine-skinned-shadow"));
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "joints", termin::MaterialPipelineValueType::Float4));
    CHECK(has_attribute(contract.vertex_inputs, "weights", termin::MaterialPipelineValueType::Float4));
    CHECK(!has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
}

TEST_CASE("Foliage material transform declares instanced resources") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::Foliage,
            termin::MaterialPipelinePassKind::Color);

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

TEST_CASE("Foliage shadow transform uses shadow template and position-only mesh input") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::FoliageShadow,
            termin::MaterialPipelinePassKind::Color);

    REQUIRE(contract.template_uuid.has_value());
    CHECK_EQ(*contract.template_uuid, std::string("termin-engine-foliage-shadow"));
    CHECK(contract.pass_kind == termin::MaterialPipelinePassKind::Shadow);
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(!has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));
}

TEST_CASE("Static transform is explicitly transitional") {
    termin::VertexTransformContract contract =
        termin::material_pipeline_builtin_vertex_transform_contract(
            termin::VertexTransformKind::StaticMesh,
            termin::MaterialPipelinePassKind::Color);

    CHECK(!contract.template_uuid.has_value());
    CHECK(has_attribute(contract.vertex_inputs, "position", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "normal", termin::MaterialPipelineValueType::Float3));
    CHECK(has_attribute(contract.vertex_inputs, "uv", termin::MaterialPipelineValueType::Float2));

    const termin::MaterialPipelineResourceDecl* draw_data =
        find_resource(contract, "draw_data");
    REQUIRE(draw_data != nullptr);
    CHECK(draw_data->owner == termin::MaterialPipelineResourceOwner::VertexTransform);
}
