#pragma once

#include <optional>
#include <string>
#include <vector>

#include <termin/render/material_pipeline_contracts.hpp>
#include <termin/render/render_export.hpp>
#include <termin/render/vertex_transform_contracts.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

namespace termin {

struct MaterialPipelineMaterialContract {
    TcShader shader;
    MaterialFragmentInterface required_fragment_input;
    std::vector<MaterialPipelineResourceDecl> resources;
};

struct MaterialPipelinePassContract {
    std::string debug_name;
    MaterialFragmentInterface required_material_fragment_input;
    bool uses_material_fragment = true;
    std::string fragment_source_override;
    std::string fragment_entry_override = "fs_main";
    std::vector<MaterialPipelineResourceDecl> resources;
    std::optional<VertexTransformContract> static_vertex_transform;
    std::optional<VertexTransformContract> skinned_vertex_transform;
    std::optional<VertexTransformContract> foliage_vertex_transform;
};

struct MaterialPipelineShaderAssemblyRequest {
    MaterialPipelineMaterialContract material;
    VertexTransformContract vertex_transform;
    MaterialPipelinePassContract pass;

    std::string shader_name;
    std::string shader_uuid;
    std::string vertex_source_override;
    std::string vertex_entry_override;
    std::string geometry_source_override;
    std::string geometry_entry_override;

    tc_shader_language language = TC_SHADER_LANGUAGE_SLANG;
    tc_shader_artifact_policy artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
};

struct MaterialPipelineShaderAssemblyResult {
    TcShader shader;
    std::vector<MaterialPipelineDiagnostic> diagnostics;

    bool ok() const {
        return shader.is_valid() && diagnostics.empty();
    }
};

RENDER_API MaterialPipelineMaterialContract material_pipeline_material_contract_from_shader(
    TcShader shader,
    MaterialFragmentInterface required_fragment_input);

RENDER_API MaterialPipelineShaderAssemblyResult material_pipeline_assemble_shader(
    const MaterialPipelineShaderAssemblyRequest& request);

} // namespace termin
