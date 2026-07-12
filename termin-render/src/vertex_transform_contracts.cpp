#include <termin/render/vertex_transform_contracts.hpp>

#include <algorithm>
#include <utility>

namespace termin {
namespace {

MaterialPipelineSemantic semantic(
    std::string name,
    MaterialPipelineValueType type)
{
    return {std::move(name), type};
}

MaterialPipelineResourceDecl resource(
    std::string name,
    uint32_t kind,
    uint32_t scope,
    uint32_t stage_mask,
    uint32_t size = 0)
{
    MaterialPipelineResourceDecl result{};
    result.requirement.name = std::move(name);
    result.requirement.kind = kind;
    result.requirement.scope = scope;
    result.requirement.stage_mask = stage_mask;
    result.requirement.size = size;
    result.owner = MaterialPipelineResourceOwner::VertexTransform;
    return result;
}

void append_bone_block(VertexTransformContract& contract)
{
    contract.resources.push_back(material_pipeline_abi_resource_decl(
        ShaderAbiResourceId::BoneBlock,
        TC_SHADER_STAGE_VERTEX,
        MaterialPipelineResourceOwner::VertexTransform));
}

} // namespace

const char* vertex_transform_kind_name(VertexTransformKind kind)
{
    switch (kind) {
    case VertexTransformKind::StaticMesh:
        return "static_mesh";
    case VertexTransformKind::SkinnedMesh:
        return "skinned_mesh";
    case VertexTransformKind::Foliage:
        return "foliage";
    case VertexTransformKind::FoliageShadow:
        return "foliage_shadow";
    }
    return "unknown";
}

const char* material_pipeline_value_type_name(MaterialPipelineValueType type)
{
    switch (type) {
    case MaterialPipelineValueType::Float:
        return "float";
    case MaterialPipelineValueType::Float2:
        return "float2";
    case MaterialPipelineValueType::Float3:
        return "float3";
    case MaterialPipelineValueType::Float4:
        return "float4";
    case MaterialPipelineValueType::Matrix4:
        return "matrix4";
    }
    return "unknown";
}

bool vertex_transform_provider_is_modular(
    const VertexTransformProvider& provider)
{
    return !provider.source_module.module_name.empty() &&
           !provider.source_module.source_identity.empty() &&
           !provider.entry_input_declaration.empty() &&
           !provider.adapter_input_expression.empty();
}

MaterialFragmentInterface material_pipeline_standard_material_fragment_interface()
{
    MaterialFragmentInterface interface;
    interface.semantics = {
        semantic("world_pos", MaterialPipelineValueType::Float3),
        semantic("normal_world", MaterialPipelineValueType::Float3),
        semantic("uv", MaterialPipelineValueType::Float2),
        semantic("tangent_world", MaterialPipelineValueType::Float3),
        semantic("bitangent_world", MaterialPipelineValueType::Float3),
        semantic("tbn_valid", MaterialPipelineValueType::Float),
    };
    return interface;
}

VertexInputContract material_pipeline_full_material_mesh_input()
{
    VertexInputContract input;
    input.mesh_attributes = {
        semantic("position", MaterialPipelineValueType::Float3),
        semantic("normal", MaterialPipelineValueType::Float3),
        semantic("uv", MaterialPipelineValueType::Float2),
        semantic("tangent", MaterialPipelineValueType::Float4),
    };
    return input;
}

VertexInputContract material_pipeline_position_mesh_input()
{
    VertexInputContract input;
    input.mesh_attributes = {
        semantic("position", MaterialPipelineValueType::Float3),
    };
    return input;
}

VertexInputContract material_pipeline_position_normal_mesh_input()
{
    VertexInputContract input = material_pipeline_position_mesh_input();
    input.mesh_attributes.push_back(
        semantic("normal", MaterialPipelineValueType::Float3));
    return input;
}

VertexInputContract material_pipeline_skinned_material_mesh_input()
{
    VertexInputContract input = material_pipeline_full_material_mesh_input();
    input.mesh_attributes.push_back(
        semantic("joints", MaterialPipelineValueType::Float4));
    input.mesh_attributes.push_back(
        semantic("weights", MaterialPipelineValueType::Float4));
    return input;
}

VertexInputContract material_pipeline_skinned_position_mesh_input()
{
    VertexInputContract input = material_pipeline_position_mesh_input();
    input.mesh_attributes.push_back(
        semantic("joints", MaterialPipelineValueType::Float4));
    input.mesh_attributes.push_back(
        semantic("weights", MaterialPipelineValueType::Float4));
    return input;
}

VertexInputContract material_pipeline_skinned_position_normal_mesh_input()
{
    VertexInputContract input = material_pipeline_position_normal_mesh_input();
    input.mesh_attributes.push_back(
        semantic("joints", MaterialPipelineValueType::Float4));
    input.mesh_attributes.push_back(
        semantic("weights", MaterialPipelineValueType::Float4));
    return input;
}

VertexInputContract material_pipeline_foliage_material_mesh_input()
{
    VertexInputContract input;
    input.mesh_attributes = {
        semantic("position", MaterialPipelineValueType::Float3),
        semantic("normal", MaterialPipelineValueType::Float3),
        semantic("uv", MaterialPipelineValueType::Float2),
    };
    return input;
}

MaterialPipelineResourceDecl material_pipeline_draw_resource_decl(
    std::string name,
    uint32_t stage_mask,
    uint32_t size)
{
    return resource(
        std::move(name),
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        stage_mask,
        size);
}

std::vector<MaterialPipelineResourceDecl> material_pipeline_common_vertex_resources(
    std::string draw_resource_name,
    uint32_t draw_resource_size)
{
    std::vector<MaterialPipelineResourceDecl> resources;
    resources.push_back(material_pipeline_abi_resource_decl(
        ShaderAbiResourceId::PerFrame,
        TC_SHADER_STAGE_VERTEX,
        MaterialPipelineResourceOwner::VertexTransform));
    resources.push_back(material_pipeline_draw_resource_decl(
        std::move(draw_resource_name),
        TC_SHADER_STAGE_VERTEX,
        draw_resource_size));
    return resources;
}

std::vector<MaterialPipelineResourceDecl> material_pipeline_foliage_vertex_resources()
{
    std::vector<MaterialPipelineResourceDecl> resources;
    resources.push_back(material_pipeline_abi_resource_decl(
        ShaderAbiResourceId::PerFrame,
        TC_SHADER_STAGE_VERTEX,
        MaterialPipelineResourceOwner::VertexTransform));
    resources.push_back(resource(
        "foliage_draw",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        TC_SHADER_STAGE_VERTEX,
        128u));
    resources.push_back(resource(
        "foliage_instances",
        TC_SHADER_RESOURCE_STORAGE_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        TC_SHADER_STAGE_VERTEX));
    return resources;
}

VertexTransformContract material_pipeline_make_static_vertex_transform_contract(
    std::string debug_name,
    VertexInputContract vertex_inputs,
    MaterialFragmentInterface produced_fragment_input,
    std::vector<MaterialPipelineResourceDecl> resources)
{
    VertexTransformContract contract;
    contract.kind = VertexTransformKind::StaticMesh;
    contract.debug_name = std::move(debug_name);
    contract.vertex_entry = "vs_main";
    contract.vertex_inputs = std::move(vertex_inputs);
    contract.produced_fragment_input = std::move(produced_fragment_input);
    contract.resources = std::move(resources);
    return contract;
}

VertexTransformContract material_pipeline_make_skinned_vertex_transform_contract(
    const VertexTransformContract& static_contract,
    std::string debug_name,
    std::optional<std::string> template_uuid,
    VertexInputContract vertex_inputs)
{
    VertexTransformContract contract = static_contract;
    contract.kind = VertexTransformKind::SkinnedMesh;
    contract.debug_name = std::move(debug_name);
    contract.template_uuid = std::move(template_uuid);
    contract.vertex_inputs = std::move(vertex_inputs);
    append_bone_block(contract);
    return contract;
}

VertexTransformContract material_pipeline_make_foliage_vertex_transform_contract(
    VertexTransformKind kind,
    std::string debug_name,
    std::string template_uuid,
    VertexInputContract vertex_inputs,
    MaterialFragmentInterface produced_fragment_input,
    std::vector<MaterialPipelineResourceDecl> resources)
{
    VertexTransformContract contract;
    contract.kind = kind;
    contract.debug_name = std::move(debug_name);
    contract.template_uuid = std::move(template_uuid);
    contract.vertex_entry = "vs_main";
    contract.vertex_inputs = std::move(vertex_inputs);
    contract.produced_fragment_input = std::move(produced_fragment_input);
    contract.resources = std::move(resources);
    if (kind == VertexTransformKind::Foliage ||
        kind == VertexTransformKind::FoliageShadow) {
        contract.instance_streams.push_back({"foliage_instances", 32u});
    }
    return contract;
}

bool material_pipeline_interface_produces(
    const MaterialFragmentInterface& interface,
    const std::string& semantic_name,
    MaterialPipelineValueType type)
{
    return std::any_of(
        interface.semantics.begin(),
        interface.semantics.end(),
        [&](const MaterialPipelineSemantic& semantic) {
            return semantic.name == semantic_name && semantic.type == type;
        });
}

} // namespace termin
