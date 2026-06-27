#include <termin/render/material_pipeline_variant.hpp>

namespace termin {
namespace {

MaterialPipelineDiagnostic missing_semantic_diagnostic(
    const MaterialPipelineSemantic& semantic,
    const MaterialPipelineVariantRequest& request)
{
    MaterialPipelineDiagnostic diagnostic{};
    diagnostic.code = MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic;
    diagnostic.message =
        "vertex transform '" + request.vertex_transform.debug_name +
        "' does not produce fragment input semantic '" + semantic.name +
        "' of type " + material_pipeline_value_type_name(semantic.type);
    return diagnostic;
}

void validate_fragment_interface(
    const MaterialFragmentInterface& required,
    const MaterialPipelineVariantRequest& request,
    std::vector<MaterialPipelineDiagnostic>& diagnostics)
{
    for (const MaterialPipelineSemantic& semantic : required.semantics) {
        if (!material_pipeline_interface_produces(
                request.vertex_transform.produced_fragment_input,
                semantic.name,
                semantic.type)) {
            diagnostics.push_back(missing_semantic_diagnostic(semantic, request));
        }
    }
}

void append_resources(
    std::vector<MaterialPipelineResourceDecl>& merged,
    std::vector<MaterialPipelineDiagnostic>& diagnostics,
    const std::vector<MaterialPipelineResourceDecl>& resources)
{
    for (const MaterialPipelineResourceDecl& resource : resources) {
        material_pipeline_merge_resource(merged, resource, diagnostics);
    }
}

} // namespace

PassContract material_pipeline_builtin_pass_contract(
    MaterialPipelinePassKind kind)
{
    PassContract contract;
    contract.kind = kind;
    contract.debug_name = material_pipeline_pass_kind_name(kind);

    switch (kind) {
    case MaterialPipelinePassKind::Color:
        contract.uses_material_fragment = true;
        break;
    case MaterialPipelinePassKind::Shadow:
    case MaterialPipelinePassKind::Depth:
    case MaterialPipelinePassKind::DepthOnly:
    case MaterialPipelinePassKind::Id:
    case MaterialPipelinePassKind::Normal:
        contract.uses_material_fragment = false;
        break;
    }

    return contract;
}

MaterialPipelineVariantPlan material_pipeline_plan_variant(
    const MaterialPipelineVariantRequest& request)
{
    MaterialPipelineVariantPlan plan;
    plan.pass_kind = request.pass.kind;
    plan.vertex_transform_kind = request.vertex_transform.kind;

    const MaterialFragmentInterface& required_fragment_input =
        request.pass.uses_material_fragment
            ? request.material.fragment_input
            : request.pass.fragment_input;
    validate_fragment_interface(required_fragment_input, request, plan.diagnostics);

    append_resources(plan.resources, plan.diagnostics, request.material.resources);
    append_resources(plan.resources, plan.diagnostics, request.vertex_transform.resources);
    append_resources(plan.resources, plan.diagnostics, request.pass.resources);

    return plan;
}

} // namespace termin
