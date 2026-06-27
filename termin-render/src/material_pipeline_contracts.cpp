#include <termin/render/material_pipeline_contracts.hpp>

#include <cstdio>

namespace termin {
namespace {

bool same_resource_identity(
    const MaterialPipelineResourceDecl& a,
    const MaterialPipelineResourceDecl& b)
{
    return a.requirement.name == b.requirement.name &&
           a.requirement.kind == b.requirement.kind &&
           a.requirement.scope == b.requirement.scope &&
           (!a.placement.resolved ||
            !b.placement.resolved ||
            (a.placement.set == b.placement.set &&
             a.placement.binding == b.placement.binding));
}

bool same_resource_name_different_contract(
    const MaterialPipelineResourceDecl& a,
    const MaterialPipelineResourceDecl& b)
{
    return a.requirement.name == b.requirement.name &&
           (a.requirement.kind != b.requirement.kind ||
            a.requirement.scope != b.requirement.scope);
}

bool same_resource_name_different_placement(
    const MaterialPipelineResourceDecl& a,
    const MaterialPipelineResourceDecl& b)
{
    return a.requirement.name == b.requirement.name &&
           a.placement.resolved &&
           b.placement.resolved &&
           (a.placement.set != b.placement.set ||
            a.placement.binding != b.placement.binding);
}

bool same_resource_placement_different_name(
    const MaterialPipelineResourceDecl& a,
    const MaterialPipelineResourceDecl& b)
{
    return a.placement.resolved &&
           b.placement.resolved &&
           a.placement.set == b.placement.set &&
           a.placement.binding == b.placement.binding &&
           a.requirement.name != b.requirement.name;
}

std::string resource_label(const MaterialPipelineResourceDecl& resource)
{
    char buffer[256];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "'%s' owner=%s kind=%u scope=%u placement=%s set=%u binding=%u",
        resource.requirement.name.c_str(),
        material_pipeline_resource_owner_name(resource.owner),
        resource.requirement.kind,
        resource.requirement.scope,
        resource.placement.resolved ? "resolved" : "unresolved",
        resource.placement.set,
        resource.placement.binding);
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
    case MaterialPipelineDiagnosticCode::ResourcePlacementConflict:
        return "resource_placement_conflict";
    case MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic:
        return "missing_vertex_output_semantic";
    case MaterialPipelineDiagnosticCode::MissingVertexTransformTemplate:
        return "missing_vertex_transform_template";
    case MaterialPipelineDiagnosticCode::MissingFragmentSource:
        return "missing_fragment_source";
    case MaterialPipelineDiagnosticCode::MissingResourcePlacement:
        return "missing_resource_placement";
    case MaterialPipelineDiagnosticCode::ShaderCreationFailed:
        return "shader_creation_failed";
    }
    return "unknown";
}

bool material_pipeline_merge_resource(
    std::vector<MaterialPipelineResourceDecl>& resources,
    const MaterialPipelineResourceDecl& incoming,
    std::vector<MaterialPipelineDiagnostic>& diagnostics)
{
    for (MaterialPipelineResourceDecl& existing : resources) {
        if (same_resource_identity(existing, incoming)) {
            existing.requirement.stage_mask |= incoming.requirement.stage_mask;
            if (incoming.requirement.size > existing.requirement.size) {
                existing.requirement.size = incoming.requirement.size;
            }
            if (!existing.placement.resolved && incoming.placement.resolved) {
                existing.placement = incoming.placement;
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

        if (same_resource_name_different_placement(existing, incoming)) {
            add_diagnostic(
                diagnostics,
                MaterialPipelineDiagnosticCode::ResourcePlacementConflict,
                existing,
                incoming,
                "resource name is assigned incompatible backend placement");
            return false;
        }

        if (same_resource_placement_different_name(existing, incoming)) {
            add_diagnostic(
                diagnostics,
                MaterialPipelineDiagnosticCode::ResourcePlacementConflict,
                existing,
                incoming,
                "resource placement is claimed by different names");
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
