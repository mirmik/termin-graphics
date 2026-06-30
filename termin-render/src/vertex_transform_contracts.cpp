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

MaterialFragmentInterface make_standard_material_fragment_interface()
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

VertexInputContract full_material_mesh_input()
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

VertexInputContract skinned_material_mesh_input()
{
    VertexInputContract input = full_material_mesh_input();
    input.mesh_attributes.push_back(
        semantic("joints", MaterialPipelineValueType::Float4));
    input.mesh_attributes.push_back(
        semantic("weights", MaterialPipelineValueType::Float4));
    return input;
}

VertexInputContract foliage_material_mesh_input()
{
    VertexInputContract input;
    input.mesh_attributes = {
        semantic("position", MaterialPipelineValueType::Float3),
        semantic("normal", MaterialPipelineValueType::Float3),
        semantic("uv", MaterialPipelineValueType::Float2),
    };
    return input;
}

VertexInputContract position_mesh_input()
{
    VertexInputContract input;
    input.mesh_attributes = {
        semantic("position", MaterialPipelineValueType::Float3),
    };
    return input;
}

VertexInputContract skinned_position_mesh_input()
{
    VertexInputContract input = position_mesh_input();
    input.mesh_attributes.push_back(
        semantic("joints", MaterialPipelineValueType::Float4));
    input.mesh_attributes.push_back(
        semantic("weights", MaterialPipelineValueType::Float4));
    return input;
}

VertexInputContract position_normal_mesh_input()
{
    VertexInputContract input = position_mesh_input();
    input.mesh_attributes.push_back(
        semantic("normal", MaterialPipelineValueType::Float3));
    return input;
}

VertexInputContract skinned_position_normal_mesh_input()
{
    VertexInputContract input = position_normal_mesh_input();
    input.mesh_attributes.push_back(
        semantic("joints", MaterialPipelineValueType::Float4));
    input.mesh_attributes.push_back(
        semantic("weights", MaterialPipelineValueType::Float4));
    return input;
}

void add_engine_per_frame(VertexTransformContract& contract)
{
    contract.resources.push_back(material_pipeline_abi_resource_decl(
        ShaderAbiResourceId::PerFrame,
        TC_SHADER_STAGE_VERTEX,
        MaterialPipelineResourceOwner::VertexTransform));
}

void add_static_draw_data(VertexTransformContract& contract, const char* name)
{
    contract.resources.push_back(resource(
        name,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        TC_SHADER_STAGE_VERTEX,
        64u));
}

void add_bone_block(VertexTransformContract& contract)
{
    contract.resources.push_back(material_pipeline_abi_resource_decl(
        ShaderAbiResourceId::BoneBlock,
        TC_SHADER_STAGE_VERTEX,
        MaterialPipelineResourceOwner::VertexTransform));
}

void add_foliage_resources(VertexTransformContract& contract)
{
    contract.resources.push_back(resource(
        "foliage_draw",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        TC_SHADER_STAGE_VERTEX,
        128u));
    contract.resources.push_back(resource(
        "foliage_instances",
        TC_SHADER_RESOURCE_STORAGE_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        TC_SHADER_STAGE_VERTEX));
    contract.instance_streams.push_back({"foliage_instances", 32u});
}

VertexTransformContract static_contract(MaterialPipelinePassKind pass_kind)
{
    VertexTransformContract contract;
    contract.kind = VertexTransformKind::StaticMesh;
    contract.pass_kind = pass_kind;
    contract.debug_name = "static";
    contract.vertex_entry = "vs_main";
    contract.produced_fragment_input =
        make_standard_material_fragment_interface();

    switch (pass_kind) {
    case MaterialPipelinePassKind::Shadow:
        contract.vertex_inputs = position_mesh_input();
        add_engine_per_frame(contract);
        add_static_draw_data(contract, "shadow_draw");
        break;
    case MaterialPipelinePassKind::Depth:
    case MaterialPipelinePassKind::DepthOnly:
        contract.vertex_inputs = position_mesh_input();
        add_engine_per_frame(contract);
        add_static_draw_data(contract, "depth_draw");
        break;
    case MaterialPipelinePassKind::Id:
        contract.vertex_inputs = position_mesh_input();
        add_engine_per_frame(contract);
        add_static_draw_data(contract, "id_draw");
        break;
    case MaterialPipelinePassKind::Normal:
        contract.vertex_inputs = position_normal_mesh_input();
        add_engine_per_frame(contract);
        add_static_draw_data(contract, "normal_draw");
        break;
    case MaterialPipelinePassKind::Color:
        contract.vertex_inputs = full_material_mesh_input();
        add_engine_per_frame(contract);
        contract.resources.push_back(material_pipeline_abi_resource_decl(
            ShaderAbiResourceId::DrawData,
            TC_SHADER_STAGE_VERTEX,
            MaterialPipelineResourceOwner::VertexTransform,
            64u));
        break;
    }

    return contract;
}

VertexTransformContract skinned_contract(MaterialPipelinePassKind pass_kind)
{
    VertexTransformContract contract = static_contract(pass_kind);
    contract.kind = VertexTransformKind::SkinnedMesh;
    contract.debug_name = "skinned";

    switch (pass_kind) {
    case MaterialPipelinePassKind::Shadow:
        contract.template_uuid = "termin-engine-skinned-shadow";
        contract.vertex_inputs = skinned_position_mesh_input();
        break;
    case MaterialPipelinePassKind::Depth:
    case MaterialPipelinePassKind::DepthOnly:
        contract.template_uuid = "termin-engine-skinned-depth";
        contract.vertex_inputs = skinned_position_mesh_input();
        break;
    case MaterialPipelinePassKind::Id:
        contract.template_uuid = "termin-engine-skinned-id";
        contract.vertex_inputs = skinned_position_mesh_input();
        break;
    case MaterialPipelinePassKind::Normal:
        contract.template_uuid = "termin-engine-skinned-normal";
        contract.vertex_inputs = skinned_position_normal_mesh_input();
        break;
    case MaterialPipelinePassKind::Color:
        contract.template_uuid = "termin-engine-skinned-material";
        contract.vertex_inputs = skinned_material_mesh_input();
        break;
    }

    add_bone_block(contract);
    return contract;
}

VertexTransformContract foliage_contract(MaterialPipelinePassKind pass_kind)
{
    VertexTransformContract contract;
    contract.kind = pass_kind == MaterialPipelinePassKind::Shadow
        ? VertexTransformKind::FoliageShadow
        : VertexTransformKind::Foliage;
    contract.pass_kind = pass_kind;
    contract.debug_name = pass_kind == MaterialPipelinePassKind::Shadow
        ? "foliage_shadow"
        : "foliage";
    contract.template_uuid = pass_kind == MaterialPipelinePassKind::Shadow
        ? "termin-engine-foliage-shadow"
        : "termin-engine-foliage-instanced";
    contract.vertex_entry = "vs_main";
    contract.produced_fragment_input =
        make_standard_material_fragment_interface();

    if (pass_kind == MaterialPipelinePassKind::Shadow) {
        contract.vertex_inputs = position_mesh_input();
    } else {
        contract.vertex_inputs = foliage_material_mesh_input();
    }

    add_engine_per_frame(contract);
    add_foliage_resources(contract);
    return contract;
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

const char* material_pipeline_pass_kind_name(MaterialPipelinePassKind kind)
{
    switch (kind) {
    case MaterialPipelinePassKind::Color:
        return "color";
    case MaterialPipelinePassKind::Shadow:
        return "shadow";
    case MaterialPipelinePassKind::Depth:
        return "depth";
    case MaterialPipelinePassKind::DepthOnly:
        return "depth_only";
    case MaterialPipelinePassKind::Id:
        return "id";
    case MaterialPipelinePassKind::Normal:
        return "normal";
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

MaterialFragmentInterface material_pipeline_standard_material_fragment_interface()
{
    return make_standard_material_fragment_interface();
}

VertexTransformContract material_pipeline_builtin_vertex_transform_contract(
    VertexTransformKind kind,
    MaterialPipelinePassKind pass_kind)
{
    switch (kind) {
    case VertexTransformKind::StaticMesh:
        return static_contract(pass_kind);
    case VertexTransformKind::SkinnedMesh:
        return skinned_contract(pass_kind);
    case VertexTransformKind::Foliage:
    case VertexTransformKind::FoliageShadow:
        return foliage_contract(
            kind == VertexTransformKind::FoliageShadow
                ? MaterialPipelinePassKind::Shadow
                : pass_kind);
    }
    return static_contract(pass_kind);
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
