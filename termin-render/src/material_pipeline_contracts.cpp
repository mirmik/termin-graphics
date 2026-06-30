#include <termin/render/material_pipeline_contracts.hpp>

#include <cstdio>
#include <utility>

namespace termin {
namespace {

bool same_resource_identity(
    const MaterialPipelineResourceDecl& a,
    const MaterialPipelineResourceDecl& b)
{
    return a.requirement.name == b.requirement.name &&
           a.requirement.kind == b.requirement.kind &&
           a.requirement.scope == b.requirement.scope;
}

bool same_resource_name_different_contract(
    const MaterialPipelineResourceDecl& a,
    const MaterialPipelineResourceDecl& b)
{
    return a.requirement.name == b.requirement.name &&
           (a.requirement.kind != b.requirement.kind ||
            a.requirement.scope != b.requirement.scope);
}

std::string resource_label(const MaterialPipelineResourceDecl& resource)
{
    char buffer[256];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "'%s' owner=%s kind=%u scope=%u",
        resource.requirement.name.c_str(),
        material_pipeline_resource_owner_name(resource.owner),
        resource.requirement.kind,
        resource.requirement.scope);
    return std::string(buffer);
}

void add_diagnostic(
    std::vector<MaterialPipelineDiagnostic>& diagnostics,
    MaterialPipelineDiagnosticCode code,
    const MaterialPipelineResourceDecl& existing,
    const MaterialPipelineResourceDecl& incoming,
    const char* reason)
{
    MaterialPipelineDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.existing = existing;
    diagnostic.incoming = incoming;
    diagnostic.message = std::string(reason) + ": existing " +
        resource_label(existing) + " vs incoming " + resource_label(incoming);
    diagnostics.push_back(std::move(diagnostic));
}

MaterialPipelineResourceDecl abi_expected_resource_decl(
    const ShaderAbiResourceDecl& abi,
    const MaterialPipelineResourceDecl& incoming)
{
    MaterialPipelineResourceDecl result = incoming;
    result.requirement.name = std::string(abi.canonical_name);
    result.requirement.kind = abi.kind;
    result.requirement.scope = abi.scope;
    return result;
}

bool canonicalize_and_validate_abi_resource(
    MaterialPipelineResourceDecl& resource,
    std::vector<MaterialPipelineDiagnostic>& diagnostics)
{
    const ShaderAbiResourceDecl* abi =
        find_shader_abi_resource(resource.requirement.name);
    if (!abi) {
        return true;
    }

    if (resource.requirement.kind != abi->kind ||
        resource.requirement.scope != abi->scope) {
        add_diagnostic(
            diagnostics,
            MaterialPipelineDiagnosticCode::AbiResourceContractMismatch,
            abi_expected_resource_decl(*abi, resource),
            resource,
            "shader ABI resource is declared with incompatible contract");
        return false;
    }

    resource.requirement.name = std::string(abi->canonical_name);
    return true;
}

} // namespace

const char* material_pipeline_resource_owner_name(
    MaterialPipelineResourceOwner owner)
{
    switch (owner) {
    case MaterialPipelineResourceOwner::Material:
        return "material";
    case MaterialPipelineResourceOwner::VertexTransform:
        return "vertex_transform";
    case MaterialPipelineResourceOwner::Pass:
        return "pass";
    case MaterialPipelineResourceOwner::RuntimeBackend:
        return "runtime_backend";
    case MaterialPipelineResourceOwner::LegacyGlsl:
        return "legacy_glsl";
    }
    return "unknown";
}

const char* material_pipeline_diagnostic_code_name(
    MaterialPipelineDiagnosticCode code)
{
    switch (code) {
    case MaterialPipelineDiagnosticCode::None:
        return "none";
    case MaterialPipelineDiagnosticCode::ResourceNameConflict:
        return "resource_name_conflict";
    case MaterialPipelineDiagnosticCode::AbiResourceContractMismatch:
        return "abi_resource_contract_mismatch";
    case MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic:
        return "missing_vertex_output_semantic";
    case MaterialPipelineDiagnosticCode::MissingVertexTransformTemplate:
        return "missing_vertex_transform_template";
    case MaterialPipelineDiagnosticCode::MissingFragmentSource:
        return "missing_fragment_source";
    case MaterialPipelineDiagnosticCode::ShaderCreationFailed:
        return "shader_creation_failed";
    }
    return "unknown";
}

MaterialPipelineResourceDecl material_pipeline_abi_resource_decl(
    ShaderAbiResourceId id,
    uint32_t stage_mask,
    MaterialPipelineResourceOwner owner,
    uint32_t size)
{
    const ShaderAbiResourceDecl& abi = shader_abi_resource(id);
    MaterialPipelineResourceDecl result{};
    result.requirement.name = std::string(abi.canonical_name);
    result.requirement.kind = abi.kind;
    result.requirement.scope = abi.scope;
    result.requirement.stage_mask = stage_mask;
    result.requirement.size = size;
    result.owner = owner;
    return result;
}

bool material_pipeline_merge_resource(
    std::vector<MaterialPipelineResourceDecl>& resources,
    const MaterialPipelineResourceDecl& incoming_resource,
    std::vector<MaterialPipelineDiagnostic>& diagnostics)
{
    MaterialPipelineResourceDecl incoming = incoming_resource;
    if (!canonicalize_and_validate_abi_resource(incoming, diagnostics)) {
        return false;
    }

    for (MaterialPipelineResourceDecl& existing : resources) {
        if (same_resource_identity(existing, incoming)) {
            existing.requirement.stage_mask |= incoming.requirement.stage_mask;
            if (incoming.requirement.size > existing.requirement.size) {
                existing.requirement.size = incoming.requirement.size;
            }
            return true;
        }

        if (same_resource_name_different_contract(existing, incoming)) {
            add_diagnostic(
                diagnostics,
                MaterialPipelineDiagnosticCode::ResourceNameConflict,
                existing,
                incoming,
                "resource name is declared with incompatible contract");
            return false;
        }

    }

    resources.push_back(incoming);
    return true;
}

MaterialPipelineResourceMergeResult material_pipeline_merge_resources(
    std::span<const MaterialPipelineResourceDecl> resources)
{
    MaterialPipelineResourceMergeResult result;
    result.resources.reserve(resources.size());
    for (const MaterialPipelineResourceDecl& resource : resources) {
        material_pipeline_merge_resource(
            result.resources,
            resource,
            result.diagnostics);
    }
    return result;
}

} // namespace termin
