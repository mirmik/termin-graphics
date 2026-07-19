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

MaterialFragmentInterface world_position_interface()
{
    MaterialFragmentInterface interface;
    interface.semantics.push_back(
        semantic("world_pos", MaterialPipelineValueType::Float3));
    return interface;
}

MaterialFragmentInterface world_position_normal_interface()
{
    MaterialFragmentInterface interface = world_position_interface();
    interface.semantics.push_back(
        semantic("normal_world", MaterialPipelineValueType::Float3));
    return interface;
}

VertexTransformProvider make_mesh_vertex_transform_provider(
    VertexTransformKind kind,
    std::string debug_name,
    MeshVertexTransformProfile profile,
    std::string model_expression)
{
    VertexTransformProvider provider;
    provider.kind = kind;
    provider.debug_name = std::move(debug_name);
    provider.vertex_entry = "vs_main";
    provider.source_module = {
        "termin_vertex_transform",
        "builtin_shaders/termin_vertex_transform.slang"};

    const bool skinned = kind == VertexTransformKind::SkinnedMesh;
    switch (profile) {
    case MeshVertexTransformProfile::Material:
        provider.vertex_inputs = skinned
            ? material_pipeline_skinned_material_mesh_input()
            : material_pipeline_full_material_mesh_input();
        provider.produced_fragment_input =
            material_pipeline_standard_material_fragment_interface();
        provider.produced_world_semantics =
            material_pipeline_standard_material_fragment_interface();
        provider.entry_input_declaration = skinned ? R"(
struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    float4 joints : BLENDINDICES0;
    float4 weights : BLENDWEIGHT0;
};)" : R"(
struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};)";
        provider.adapter_input_expression = skinned
            ? "termin_skinned_world_vertex(input.position, input.normal, input.uv, input.tangent, input.joints, input.weights, " + model_expression + ", bone_block)"
            : "termin_static_world_vertex(input.position, input.normal, input.uv, input.tangent, " + model_expression + ")";
        break;
    case MeshVertexTransformProfile::Position:
        provider.vertex_inputs = skinned
            ? material_pipeline_skinned_position_mesh_input()
            : material_pipeline_position_mesh_input();
        provider.produced_world_semantics = world_position_interface();
        provider.entry_input_declaration = skinned ? R"(
struct VertexInput {
    float3 position : POSITION;
    float4 joints : TEXCOORD0;
    float4 weights : TEXCOORD1;
};)" : R"(
struct VertexInput {
    float3 position : POSITION;
};)";
        provider.adapter_input_expression = skinned
            ? "termin_skinned_world_position(input.position, input.joints, input.weights, " + model_expression + ", bone_block)"
            : "termin_static_world_position(input.position, " + model_expression + ")";
        break;
    case MeshVertexTransformProfile::PositionNormal:
        provider.vertex_inputs = skinned
            ? material_pipeline_skinned_position_normal_mesh_input()
            : material_pipeline_position_normal_mesh_input();
        provider.produced_world_semantics = world_position_normal_interface();
        provider.entry_input_declaration = skinned ? R"(
struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 joints : TEXCOORD0;
    float4 weights : TEXCOORD1;
};)" : R"(
struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
};)";
        provider.adapter_input_expression = skinned
            ? "termin_skinned_world_position_normal(input.position, input.normal, input.joints, input.weights, " + model_expression + ", bone_block)"
            : "termin_static_world_position_normal(input.position, input.normal, " + model_expression + ")";
        break;
    }

    if (skinned) {
        append_bone_block(provider);
    }
    return provider;
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

std::vector<MaterialPipelineResourceDecl> material_pipeline_pass_vertex_resources(
    std::string draw_resource_name,
    uint32_t draw_resource_size)
{
    std::vector<MaterialPipelineResourceDecl> resources;
    resources.push_back(material_pipeline_abi_resource_decl(
        ShaderAbiResourceId::PerFrame,
        TC_SHADER_STAGE_VERTEX,
        MaterialPipelineResourceOwner::Pass));
    MaterialPipelineResourceDecl draw = material_pipeline_draw_resource_decl(
        std::move(draw_resource_name),
        TC_SHADER_STAGE_VERTEX,
        draw_resource_size);
    draw.owner = MaterialPipelineResourceOwner::Pass;
    resources.push_back(std::move(draw));
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

VertexTransformProvider material_pipeline_make_static_mesh_vertex_transform_provider(
    std::string debug_name,
    MeshVertexTransformProfile profile,
    std::string model_expression)
{
    return make_mesh_vertex_transform_provider(
        VertexTransformKind::StaticMesh,
        std::move(debug_name),
        profile,
        std::move(model_expression));
}

VertexTransformProvider material_pipeline_make_skinned_mesh_vertex_transform_provider(
    std::string debug_name,
    MeshVertexTransformProfile profile,
    std::string model_expression)
{
    return make_mesh_vertex_transform_provider(
        VertexTransformKind::SkinnedMesh,
        std::move(debug_name),
        profile,
        std::move(model_expression));
}

VertexOutputAdapter material_pipeline_standard_material_vertex_output_adapter()
{
    VertexOutputAdapter adapter;
    adapter.debug_name = "standard_material_output";
    adapter.source_module = {
        "termin_material_vertex_output_adapter",
        "builtin_shaders/termin_material_vertex_output_adapter.slang"};
    adapter.output_type_name = "VertexOutput";
    adapter.output_function = "termin_material_vertex_output";
    adapter.consumed_world_semantics =
        material_pipeline_standard_material_fragment_interface();
    adapter.produced_output_semantics =
        material_pipeline_standard_material_fragment_interface();
    adapter.resources.push_back(material_pipeline_abi_resource_decl(
        ShaderAbiResourceId::PerFrame,
        TC_SHADER_STAGE_VERTEX,
        MaterialPipelineResourceOwner::Pass));
    return adapter;
}

VertexTransformProvider material_pipeline_make_foliage_material_vertex_transform_provider(
    std::string debug_name)
{
    return material_pipeline_make_foliage_vertex_transform_provider(
        std::move(debug_name),
        MeshVertexTransformProfile::Material);
}

VertexTransformProvider material_pipeline_make_foliage_vertex_transform_provider(
    std::string debug_name,
    MeshVertexTransformProfile profile)
{
    VertexTransformProvider provider;
    provider.kind = VertexTransformKind::Foliage;
    provider.debug_name = std::move(debug_name);
    provider.vertex_entry = "vs_main";
    switch (profile) {
    case MeshVertexTransformProfile::Material:
        provider.vertex_inputs = material_pipeline_foliage_material_mesh_input();
        break;
    case MeshVertexTransformProfile::Position:
        provider.vertex_inputs = material_pipeline_position_mesh_input();
        break;
    case MeshVertexTransformProfile::PositionNormal:
        provider.vertex_inputs = material_pipeline_position_normal_mesh_input();
        break;
    }
    provider.produced_fragment_input =
        material_pipeline_standard_material_fragment_interface();
    provider.produced_world_semantics =
        material_pipeline_standard_material_fragment_interface();
    provider.resources = material_pipeline_foliage_vertex_resources();
    provider.instance_streams.push_back({"foliage_instances", 32u});
    std::erase_if(
        provider.resources,
        [](const MaterialPipelineResourceDecl& resource) {
            return resource.requirement.name == TC_SHADER_RESOURCE_PER_FRAME;
        });
    provider.source_module = {
        "termin_foliage_material_transform",
        "builtin_shaders/termin_foliage_material_transform.slang"};
    switch (profile) {
    case MeshVertexTransformProfile::Material:
        provider.entry_input_declaration = R"(
struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint instance_id : SV_InstanceID;
};)";
        provider.adapter_input_expression =
            "termin_foliage_material_world_vertex("
            "input.position, input.normal, input.uv, input.instance_id)";
        break;
    case MeshVertexTransformProfile::Position:
        provider.entry_input_declaration = R"(
struct VertexInput {
    float3 position : POSITION;
    uint instance_id : SV_InstanceID;
};)";
        provider.adapter_input_expression =
            "termin_foliage_world_position(input.position, input.instance_id)";
        break;
    case MeshVertexTransformProfile::PositionNormal:
        provider.entry_input_declaration = R"(
struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    uint instance_id : SV_InstanceID;
};)";
        provider.adapter_input_expression =
            "termin_foliage_world_position_normal("
            "input.position, input.normal, input.instance_id)";
        break;
    }
    return provider;
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
