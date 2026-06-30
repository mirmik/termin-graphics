#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <termin/render/render_export.hpp>
#include <termin/render/shader_abi.hpp>

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
    AbiResourceContractMismatch,
    MissingVertexOutputSemantic,
    MissingVertexTransformTemplate,
    MissingFragmentSource,
    ShaderCreationFailed,
};

struct MaterialPipelineResourceRequirement {
    std::string name;
    uint32_t kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    uint32_t scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    uint32_t stage_mask = 0;
    uint32_t size = 0;
};

struct MaterialPipelineResourceDecl {
    MaterialPipelineResourceRequirement requirement;
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

RENDER_API MaterialPipelineResourceDecl material_pipeline_abi_resource_decl(
    ShaderAbiResourceId id,
    uint32_t stage_mask,
    MaterialPipelineResourceOwner owner,
    uint32_t size = 0);

RENDER_API bool material_pipeline_merge_resource(
    std::vector<MaterialPipelineResourceDecl>& resources,
    const MaterialPipelineResourceDecl& incoming,
    std::vector<MaterialPipelineDiagnostic>& diagnostics);

RENDER_API MaterialPipelineResourceMergeResult material_pipeline_merge_resources(
    std::span<const MaterialPipelineResourceDecl> resources);

} // namespace termin
