#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <termin/render/material_pipeline_contracts.hpp>
#include <termin/render/render_export.hpp>

namespace termin {

enum class VertexTransformKind : uint8_t {
    StaticMesh,
    SkinnedMesh,
    Foliage,
    FoliageShadow,
};

enum class MeshVertexTransformProfile : uint8_t {
    Material,
    Position,
    PositionNormal,
};

enum class MaterialPipelineValueType : uint8_t {
    Float,
    Float2,
    Float3,
    Float4,
    Matrix4,
};

struct MaterialPipelineSemantic {
    std::string name;
    MaterialPipelineValueType type = MaterialPipelineValueType::Float;
};

struct VertexInputContract {
    std::vector<MaterialPipelineSemantic> mesh_attributes;
};

struct MaterialFragmentInterface {
    std::vector<MaterialPipelineSemantic> semantics;
};

struct InstanceStreamDecl {
    std::string name;
    uint32_t stride = 0;
};

// Identifies a Slang module consumed by a composed shader stage.  The module
// name is what Slang imports; source_identity is deliberately explicit so a
// provider/adapter change also changes the assembled shader identity before a
// backend attempts to reuse an artifact.
struct ShaderSourceModuleIdentity {
    std::string module_name;
    std::string source_identity;
};

struct VertexTransformContract {
    VertexTransformKind kind = VertexTransformKind::StaticMesh;
    std::string debug_name;
    // Legacy whole-stage source selection.  New passes must use the modular
    // provider fields below; this remains while non-migrated passes are moved
    // to the composition substrate.
    std::optional<std::string> template_uuid;
    std::string vertex_entry = "vs_main";
    VertexInputContract vertex_inputs;
    MaterialFragmentInterface produced_fragment_input;
    // World-space values produced by the transform module and consumed by a
    // pass-owned output adapter.  This is separate from the material-fragment
    // interface because depth/shadow adapters have no material fragment.
    MaterialFragmentInterface produced_world_semantics;
    std::vector<MaterialPipelineResourceDecl> resources;
    std::vector<InstanceStreamDecl> instance_streams;

    // A modular provider owns its transform code and any transform-specific
    // resources (for example bone_block).  The assembler only emits imports,
    // input declaration, and this provider-owned invocation expression.
    ShaderSourceModuleIdentity source_module;
    std::string entry_input_declaration;
    std::string adapter_input_expression;
};

// Name the architectural role explicitly at new call sites without forcing a
// flag-day rename of existing pass contracts.
using VertexTransformProvider = VertexTransformContract;

struct VertexOutputAdapter {
    std::string debug_name;
    ShaderSourceModuleIdentity source_module;
    std::string output_type_name;
    std::string output_function;
    MaterialFragmentInterface consumed_world_semantics;
    MaterialFragmentInterface produced_output_semantics;
    std::vector<MaterialPipelineResourceDecl> resources;
};

RENDER_API bool vertex_transform_provider_is_modular(
    const VertexTransformProvider& provider);

RENDER_API const char* vertex_transform_kind_name(VertexTransformKind kind);
RENDER_API const char* material_pipeline_value_type_name(MaterialPipelineValueType type);

RENDER_API MaterialFragmentInterface material_pipeline_standard_material_fragment_interface();

RENDER_API VertexInputContract material_pipeline_full_material_mesh_input();
RENDER_API VertexInputContract material_pipeline_position_mesh_input();
RENDER_API VertexInputContract material_pipeline_position_normal_mesh_input();
RENDER_API VertexInputContract material_pipeline_skinned_material_mesh_input();
RENDER_API VertexInputContract material_pipeline_skinned_position_mesh_input();
RENDER_API VertexInputContract material_pipeline_skinned_position_normal_mesh_input();
RENDER_API VertexInputContract material_pipeline_foliage_material_mesh_input();

RENDER_API MaterialPipelineResourceDecl material_pipeline_draw_resource_decl(
    std::string name,
    uint32_t stage_mask,
    uint32_t size = 64u);

RENDER_API std::vector<MaterialPipelineResourceDecl> material_pipeline_common_vertex_resources(
    std::string draw_resource_name,
    uint32_t draw_resource_size = 64u);

RENDER_API std::vector<MaterialPipelineResourceDecl> material_pipeline_pass_vertex_resources(
    std::string draw_resource_name,
    uint32_t draw_resource_size = 64u);

RENDER_API std::vector<MaterialPipelineResourceDecl> material_pipeline_foliage_vertex_resources();

RENDER_API VertexTransformContract material_pipeline_make_static_vertex_transform_contract(
    std::string debug_name,
    VertexInputContract vertex_inputs,
    MaterialFragmentInterface produced_fragment_input,
    std::vector<MaterialPipelineResourceDecl> resources);

RENDER_API VertexTransformContract material_pipeline_make_skinned_vertex_transform_contract(
    const VertexTransformContract& static_contract,
    std::string debug_name,
    std::optional<std::string> template_uuid,
    VertexInputContract vertex_inputs);

RENDER_API VertexTransformProvider material_pipeline_make_static_mesh_vertex_transform_provider(
    std::string debug_name,
    MeshVertexTransformProfile profile,
    std::string model_expression);

RENDER_API VertexTransformProvider material_pipeline_make_skinned_mesh_vertex_transform_provider(
    std::string debug_name,
    MeshVertexTransformProfile profile,
    std::string model_expression);

RENDER_API VertexOutputAdapter material_pipeline_standard_material_vertex_output_adapter();

RENDER_API VertexTransformProvider material_pipeline_make_foliage_material_vertex_transform_provider(
    std::string debug_name);

RENDER_API VertexTransformProvider material_pipeline_make_foliage_vertex_transform_provider(
    std::string debug_name,
    MeshVertexTransformProfile profile);

RENDER_API VertexTransformContract material_pipeline_make_foliage_vertex_transform_contract(
    VertexTransformKind kind,
    std::string debug_name,
    std::string template_uuid,
    VertexInputContract vertex_inputs,
    MaterialFragmentInterface produced_fragment_input,
    std::vector<MaterialPipelineResourceDecl> resources);

RENDER_API bool material_pipeline_interface_produces(
    const MaterialFragmentInterface& interface,
    const std::string& semantic_name,
    MaterialPipelineValueType type);

} // namespace termin
