#pragma once

#include <vector>

#include <termin/render/render_export.hpp>
#include <termin/render/vertex_transform_contracts.hpp>

namespace termin {

struct MaterialContract {
    std::string debug_name;
    MaterialFragmentInterface fragment_input;
    std::vector<MaterialPipelineResourceDecl> resources;
};

struct PassContract {
    MaterialPipelinePassKind kind = MaterialPipelinePassKind::Color;
    std::string debug_name;
    bool uses_material_fragment = true;
    MaterialFragmentInterface fragment_input;
    std::vector<MaterialPipelineResourceDecl> resources;
};

struct MaterialPipelineVariantRequest {
    MaterialContract material;
    VertexTransformContract vertex_transform;
    PassContract pass;
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

RENDER_API PassContract material_pipeline_builtin_pass_contract(
    MaterialPipelinePassKind kind);

RENDER_API MaterialPipelineVariantPlan material_pipeline_plan_variant(
    const MaterialPipelineVariantRequest& request);

} // namespace termin
