#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

enum class MaterialPipelinePassKind : uint8_t {
    Color,
    Shadow,
    Depth,
    DepthOnly,
    Id,
    Normal,
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
    MaterialPipelinePassKind pass_kind = MaterialPipelinePassKind::Color;
    std::string debug_name;
    std::optional<std::string> template_uuid;
    std::string vertex_entry = "vs_main";
    VertexInputContract vertex_inputs;
    MaterialFragmentInterface produced_fragment_input;
    std::vector<MaterialPipelineResourceDecl> resources;
    std::vector<InstanceStreamDecl> instance_streams;
};

RENDER_API const char* vertex_transform_kind_name(VertexTransformKind kind);
RENDER_API const char* material_pipeline_pass_kind_name(MaterialPipelinePassKind kind);
RENDER_API const char* material_pipeline_value_type_name(MaterialPipelineValueType type);

RENDER_API MaterialFragmentInterface material_pipeline_standard_material_fragment_interface();

RENDER_API VertexTransformContract material_pipeline_builtin_vertex_transform_contract(
    VertexTransformKind kind,
    MaterialPipelinePassKind pass_kind);

RENDER_API bool material_pipeline_interface_produces(
    const MaterialFragmentInterface& interface,
    const std::string& semantic_name,
    MaterialPipelineValueType type);

} // namespace termin
