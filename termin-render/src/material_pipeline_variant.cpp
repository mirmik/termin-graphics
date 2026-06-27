#include <termin/render/material_pipeline_variant.hpp>

#include <tgfx/resources/tc_shader_registry.h>
#include <tgfx2/builtin_shader_sources.hpp>

#include <cstring>
#include <utility>
#include <vector>

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

void add_simple_diagnostic(
    std::vector<MaterialPipelineDiagnostic>& diagnostics,
    MaterialPipelineDiagnosticCode code,
    std::string message)
{
    MaterialPipelineDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostics.push_back(std::move(diagnostic));
}

std::vector<tc_shader_resource_binding> make_shader_resource_bindings(
    const std::vector<MaterialPipelineResourceDecl>& resources)
{
    std::vector<tc_shader_resource_binding> bindings;
    bindings.reserve(resources.size());

    for (const MaterialPipelineResourceDecl& resource : resources) {
        tc_shader_resource_binding binding{};
        std::strncpy(binding.name, resource.name.c_str(), TC_SHADER_RESOURCE_NAME_MAX - 1);
        binding.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
        binding.kind = resource.kind;
        binding.scope = resource.scope;
        binding.set = resource.set;
        binding.binding = resource.binding;
        binding.stage_mask = resource.stage_mask;
        binding.size = resource.size;
        bindings.push_back(binding);
    }

    return bindings;
}

MaterialPipelineResourceDecl resource_decl_from_binding(
    const tc_shader_resource_binding& binding,
    MaterialPipelineResourceOwner owner)
{
    MaterialPipelineResourceDecl resource{};
    resource.name = binding.name;
    resource.kind = binding.kind;
    resource.scope = binding.scope;
    resource.set = binding.set;
    resource.binding = binding.binding;
    resource.has_placement = true;
    resource.stage_mask = binding.stage_mask;
    resource.size = binding.size;
    resource.owner = owner;
    return resource;
}

bool is_material_contract_resource_binding(
    const tc_shader_resource_binding& binding)
{
    if (binding.scope == TC_SHADER_RESOURCE_SCOPE_MATERIAL) {
        return true;
    }

    if (binding.scope != TC_SHADER_RESOURCE_SCOPE_PASS) {
        return false;
    }

    return std::strcmp(binding.name, "lighting") == 0 ||
           std::strcmp(binding.name, "lighting_ubo") == 0;
}

std::string default_variant_name(const MaterialPipelineVariantRequest& request)
{
    if (!request.shader_name.empty()) {
        return request.shader_name;
    }
    std::string name = request.material.debug_name.empty()
        ? "MaterialPipelineVariant"
        : request.material.debug_name;
    name += "_";
    name += vertex_transform_kind_name(request.vertex_transform.kind);
    name += "_";
    name += material_pipeline_pass_kind_name(request.pass.kind);
    return name;
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

MaterialContract material_pipeline_material_contract_from_shader(
    TcShader shader,
    MaterialFragmentInterface fragment_input)
{
    MaterialContract contract;
    if (!shader.is_valid()) {
        return contract;
    }

    contract.debug_name = shader.name();
    contract.fragment_source = shader.fragment_source();
    contract.fragment_entry = shader.get() && shader.get()->fragment_entry
        ? shader.get()->fragment_entry
        : "fs_main";
    contract.source_path = shader.source_path();
    contract.features = shader.features();
    contract.fragment_input = std::move(fragment_input);

    const tc_shader* raw = shader.get();
    if (!raw) {
        return contract;
    }

    const tc_shader_resource_binding* bindings = tc_shader_resource_bindings(raw);
    const uint32_t count = tc_shader_resource_binding_count(raw);
    for (uint32_t i = 0; i < count; ++i) {
        if (!is_material_contract_resource_binding(bindings[i])) {
            continue;
        }
        contract.resources.push_back(
            resource_decl_from_binding(bindings[i], MaterialPipelineResourceOwner::Material));
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

MaterialPipelineCompiledVariant material_pipeline_create_variant(
    const MaterialPipelineVariantRequest& request)
{
    MaterialPipelineCompiledVariant result;
    result.plan = material_pipeline_plan_variant(request);
    if (!result.plan.ok()) {
        return result;
    }

    if (!request.vertex_transform.template_uuid.has_value() ||
        request.vertex_transform.template_uuid->empty()) {
        add_simple_diagnostic(
            result.plan.diagnostics,
            MaterialPipelineDiagnosticCode::MissingVertexTransformTemplate,
            "vertex transform '" + request.vertex_transform.debug_name +
                "' has no template uuid");
        return result;
    }

    const std::string vertex_source =
        tgfx::load_builtin_shader_stage_source_from_catalog(
            request.vertex_transform.template_uuid->c_str(),
            "vertex");
    if (vertex_source.empty()) {
        add_simple_diagnostic(
            result.plan.diagnostics,
            MaterialPipelineDiagnosticCode::MissingVertexTransformTemplate,
            "failed to load vertex transform template '" +
                *request.vertex_transform.template_uuid + "'");
        return result;
    }

    const std::string& fragment_source = request.pass.uses_material_fragment
        ? request.material.fragment_source
        : request.pass.fragment_source_override;
    if (fragment_source.empty()) {
        add_simple_diagnostic(
            result.plan.diagnostics,
            MaterialPipelineDiagnosticCode::MissingFragmentSource,
            "material pipeline variant has no fragment source");
        return result;
    }

    const std::string& fragment_entry = request.pass.uses_material_fragment
        ? request.material.fragment_entry
        : request.pass.fragment_entry_override;
    const std::string shader_name = default_variant_name(request);

    tc_shader_handle handle = tc_shader_from_sources_with_entries_ex(
        vertex_source.c_str(),
        fragment_source.c_str(),
        nullptr,
        shader_name.c_str(),
        request.material.source_path.empty() ? nullptr : request.material.source_path.c_str(),
        request.shader_uuid.empty() ? nullptr : request.shader_uuid.c_str(),
        TC_SHADER_LANGUAGE_SLANG,
        request.artifact_policy,
        request.vertex_transform.vertex_entry.empty()
            ? "vs_main"
            : request.vertex_transform.vertex_entry.c_str(),
        fragment_entry.empty() ? "fs_main" : fragment_entry.c_str(),
        nullptr);

    if (tc_shader_handle_is_invalid(handle)) {
        add_simple_diagnostic(
            result.plan.diagnostics,
            MaterialPipelineDiagnosticCode::ShaderCreationFailed,
            "tc_shader_from_sources_with_entries_ex failed for '" + shader_name + "'");
        return result;
    }

    result.shader = TcShader(handle);
    result.shader.set_features(request.material.features);
    result.shader.set_language(TC_SHADER_LANGUAGE_SLANG);
    result.shader.set_artifact_policy(request.artifact_policy);

    if (tc_shader* shader = result.shader.get()) {
        // Contract variants get field/resource metadata from shaderc sidecar
        // reflection. Parser-authored legacy material UBO entries would create
        // a second source of truth; clear them before applying the contract
        // resource layout because clearing also removes the material binding.
        tc_shader_set_material_ubo_layout(shader, nullptr, 0, 0);
        std::vector<tc_shader_resource_binding> bindings =
            make_shader_resource_bindings(result.plan.resources);
        tc_shader_set_resource_layout(
            shader,
            bindings.empty() ? nullptr : bindings.data(),
            static_cast<uint32_t>(bindings.size()));
    }

    return result;
}

} // namespace termin
