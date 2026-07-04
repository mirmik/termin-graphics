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

struct VertexTransformContract {
    VertexTransformKind kind = VertexTransformKind::StaticMesh;
    std::string debug_name;
    std::optional<std::string> template_uuid;
    std::string vertex_entry = "vs_main";
    VertexInputContract vertex_inputs;
    MaterialFragmentInterface produced_fragment_input;
    std::vector<MaterialPipelineResourceDecl> resources;
    std::vector<InstanceStreamDecl> instance_streams;
};

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

RENDER_API std::vector<MaterialPipelineResourceDecl> material_pipeline_foliage_vertex_resources();

RENDER_API VertexTransformContract material_pipeline_make_static_vertex_transform_contract(
    std::string debug_name,
    VertexInputContract vertex_inputs,
    MaterialFragmentInterface produced_fragment_input,
    std::vector<MaterialPipelineResourceDecl> resources);

RENDER_API VertexTransformContract material_pipeline_make_skinned_vertex_transform_contract(
    const VertexTransformContract& static_contract,
    std::string debug_name,
    std::string template_uuid,
    VertexInputContract vertex_inputs);

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
