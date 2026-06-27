#pragma once

#include <vector>

#include <termin/render/render_export.hpp>
#include <termin/render/vertex_transform_contracts.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

namespace termin {

struct MaterialContract {
    std::string debug_name;
    std::string fragment_source;
    std::string fragment_entry = "fs_main";
    std::string source_path;
    uint32_t features = TC_SHADER_FEATURE_NONE;
    MaterialFragmentInterface fragment_input;
    std::vector<MaterialPipelineResourceDecl> resources;
};

struct PassContract {
    MaterialPipelinePassKind kind = MaterialPipelinePassKind::Color;
    std::string debug_name;
    bool uses_material_fragment = true;
    std::string fragment_source_override;
    std::string fragment_entry_override = "fs_main";
    MaterialFragmentInterface fragment_input;
    std::vector<MaterialPipelineResourceDecl> resources;
};

struct MaterialPipelineVariantRequest {
    MaterialContract material;
    VertexTransformContract vertex_transform;
    PassContract pass;
    std::string shader_name;
    std::string shader_uuid;
    tc_shader_artifact_policy artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
};

struct MaterialPipelineVariantPlan {
    MaterialPipelinePassKind pass_kind = MaterialPipelinePassKind::Color;
    VertexTransformKind vertex_transform_kind = VertexTransformKind::StaticMesh;
    std::vector<MaterialPipelineResourceDecl> resources;
    std::vector<MaterialPipelineDiagnostic> diagnostics;

    bool ok() const {
        return diagnostics.empty();
    }
};

struct MaterialPipelineCompiledVariant {
    TcShader shader;
    MaterialPipelineVariantPlan plan;

    bool ok() const {
        return shader.is_valid() && plan.ok();
    }
};

RENDER_API PassContract material_pipeline_builtin_pass_contract(
    MaterialPipelinePassKind kind);

RENDER_API MaterialContract material_pipeline_material_contract_from_shader(
    TcShader shader,
    MaterialFragmentInterface fragment_input = {});

RENDER_API MaterialPipelineVariantPlan material_pipeline_plan_variant(
    const MaterialPipelineVariantRequest& request);

RENDER_API MaterialPipelineCompiledVariant material_pipeline_create_variant(
    const MaterialPipelineVariantRequest& request);

} // namespace termin
