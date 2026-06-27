#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <termin/render/render_export.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

namespace termin {

enum class MaterialPipelineResourceOwner : uint8_t {
    Material,
    VertexTransform,
    Pass,
    RuntimeBackend,
    LegacyGlsl,
};

enum class MaterialPipelineDiagnosticCode : uint8_t {
    None,
    ResourceNameConflict,
    ResourcePlacementConflict,
    MissingVertexOutputSemantic,
    MissingVertexTransformTemplate,
    MissingFragmentSource,
    ShaderCreationFailed,
};

struct MaterialPipelineResourceDecl {
    std::string name;
    uint32_t kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    uint32_t scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    uint32_t set = TC_SHADER_RESOURCE_SET_DEFAULT;
    uint32_t binding = 0;
    bool has_placement = true;
    uint32_t stage_mask = 0;
    uint32_t size = 0;
    MaterialPipelineResourceOwner owner = MaterialPipelineResourceOwner::Material;
};

struct MaterialPipelineDiagnostic {
    MaterialPipelineDiagnosticCode code = MaterialPipelineDiagnosticCode::None;
    std::string message;
    MaterialPipelineResourceDecl existing;
    MaterialPipelineResourceDecl incoming;
};

struct MaterialPipelineResourceMergeResult {
    std::vector<MaterialPipelineResourceDecl> resources;
    std::vector<MaterialPipelineDiagnostic> diagnostics;

    bool ok() const {
        return diagnostics.empty();
    }
};

RENDER_API const char* material_pipeline_resource_owner_name(
    MaterialPipelineResourceOwner owner);

RENDER_API const char* material_pipeline_diagnostic_code_name(
    MaterialPipelineDiagnosticCode code);

RENDER_API bool material_pipeline_merge_resource(
    std::vector<MaterialPipelineResourceDecl>& resources,
    const MaterialPipelineResourceDecl& incoming,
    std::vector<MaterialPipelineDiagnostic>& diagnostics);

RENDER_API MaterialPipelineResourceMergeResult material_pipeline_merge_resources(
    std::span<const MaterialPipelineResourceDecl> resources);

} // namespace termin
