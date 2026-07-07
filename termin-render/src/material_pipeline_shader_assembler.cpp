#include <termin/render/material_pipeline_shader_assembler.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>

#include <tcbase/tc_log.hpp>
#include <tgfx2/builtin_shader_sources.hpp>

namespace termin {
namespace {

MaterialPipelineDiagnostic diagnostic(
    MaterialPipelineDiagnosticCode code,
    std::string message)
{
    MaterialPipelineDiagnostic result{};
    result.code = code;
    result.message = std::move(message);
    return result;
}

uint32_t contract_value_type(MaterialPipelineValueType type)
{
    switch (type) {
    case MaterialPipelineValueType::Float:
        return TC_SHADER_CONTRACT_VALUE_FLOAT;
    case MaterialPipelineValueType::Float2:
        return TC_SHADER_CONTRACT_VALUE_FLOAT2;
    case MaterialPipelineValueType::Float3:
        return TC_SHADER_CONTRACT_VALUE_FLOAT3;
    case MaterialPipelineValueType::Float4:
        return TC_SHADER_CONTRACT_VALUE_FLOAT4;
    case MaterialPipelineValueType::Matrix4:
        return TC_SHADER_CONTRACT_VALUE_MATRIX4;
    }
    return TC_SHADER_CONTRACT_VALUE_UNKNOWN;
}

MaterialPipelineResourceDecl resource_decl_from_binding(
    const tc_shader_resource_binding& binding,
    MaterialPipelineResourceOwner owner)
{
    MaterialPipelineResourceDecl result{};
    result.requirement.name = binding.name;
    result.requirement.kind = binding.kind;
    result.requirement.scope = binding.scope;
    result.requirement.stage_mask = binding.stage_mask;
    result.requirement.size = binding.size;
    result.owner = owner;
    return result;
}

tc_shader_resource_requirement resource_requirement_from_decl(
    const MaterialPipelineResourceDecl& decl)
{
    tc_shader_resource_requirement requirement{};
    std::snprintf(
        requirement.name,
        sizeof(requirement.name),
        "%s",
        decl.requirement.name.c_str());
    requirement.kind = decl.requirement.kind;
    requirement.scope = decl.requirement.scope;
    requirement.stage_mask = decl.requirement.stage_mask;
    requirement.size = decl.requirement.size;
    requirement.element_stride = 0;
    return requirement;
}

void append_resources(
    std::vector<MaterialPipelineResourceDecl>& out,
    const std::vector<MaterialPipelineResourceDecl>& resources)
{
    out.insert(out.end(), resources.begin(), resources.end());
}

bool validate_fragment_interface(
    const MaterialPipelineMaterialContract& material,
    const VertexTransformContract& vertex_transform,
    std::vector<MaterialPipelineDiagnostic>& diagnostics)
{
    bool ok = true;
    for (const MaterialPipelineSemantic& semantic :
         material.required_fragment_input.semantics) {
        if (!material_pipeline_interface_produces(
                vertex_transform.produced_fragment_input,
                semantic.name,
                semantic.type)) {
            ok = false;
            diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic,
                "vertex transform '" + vertex_transform.debug_name +
                    "' does not produce material fragment semantic '" +
                    semantic.name + "'"));
        }
    }
    return ok;
}

bool append_contract_inputs(
    const VertexTransformContract& vertex_transform,
    std::vector<tc_shader_contract_vertex_input>& out)
{
    out.reserve(vertex_transform.vertex_inputs.mesh_attributes.size());
    for (const MaterialPipelineSemantic& semantic :
         vertex_transform.vertex_inputs.mesh_attributes) {
        tc_shader_contract_vertex_input input{};
        std::snprintf(
            input.semantic,
            sizeof(input.semantic),
            "%s",
            semantic.name.c_str());
        input.type = contract_value_type(semantic.type);
        input.required = 1;
        out.push_back(input);
    }
    return true;
}

void apply_instance_stream_strides(
    const VertexTransformContract& vertex_transform,
    std::vector<tc_shader_resource_requirement>& resources)
{
    for (const InstanceStreamDecl& stream : vertex_transform.instance_streams) {
        for (tc_shader_resource_requirement& resource : resources) {
            if (std::strncmp(
                    resource.name,
                    stream.name.c_str(),
                    TC_SHADER_RESOURCE_NAME_MAX) == 0) {
                resource.element_stride = stream.stride;
            }
        }
    }
}

std::string default_shader_name(const MaterialPipelineShaderAssemblyRequest& request)
{
    if (!request.shader_name.empty()) {
        return request.shader_name;
    }
    std::string name = "MaterialPipeline";
    if (!request.vertex_transform.debug_name.empty()) {
        name += "_";
        name += request.vertex_transform.debug_name;
    }
    if (!request.pass.debug_name.empty()) {
        name += "_";
        name += request.pass.debug_name;
    }
    return name;
}

} // namespace

MaterialPipelineMaterialContract material_pipeline_material_contract_from_shader(
    TcShader shader,
    MaterialFragmentInterface required_fragment_input)
{
    MaterialPipelineMaterialContract contract;
    contract.shader = shader;
    contract.required_fragment_input = std::move(required_fragment_input);

    if (tc_shader* raw = shader.get()) {
        const uint32_t count = tc_shader_resource_binding_count(raw);
        const tc_shader_resource_binding* bindings =
            tc_shader_resource_bindings(raw);
        contract.resources.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const bool fragment_visible =
                (bindings[i].stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0u;
            const bool material_scoped =
                bindings[i].scope == TC_SHADER_RESOURCE_SCOPE_MATERIAL;
            if (!fragment_visible && !material_scoped) {
                continue;
            }
            contract.resources.push_back(resource_decl_from_binding(
                bindings[i],
                MaterialPipelineResourceOwner::Material));
        }
    }

    return contract;
}

MaterialPipelineShaderAssemblyResult material_pipeline_assemble_shader(
    const MaterialPipelineShaderAssemblyRequest& request)
{
    MaterialPipelineShaderAssemblyResult result;

    if (!request.pass.uses_material_fragment &&
        request.pass.fragment_source_override.empty()) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::MissingFragmentSource,
            "pass '" + request.pass.debug_name +
                "' does not use material fragment and has no fragment override"));
        return result;
    }

    std::string vertex_source = request.vertex_source_override;
    if (vertex_source.empty() && request.vertex_transform.template_uuid.has_value()) {
        vertex_source = tgfx::load_builtin_shader_stage_source_from_catalog(
            request.vertex_transform.template_uuid->c_str(),
            "vertex");
    }

    if (vertex_source.empty()) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::MissingVertexTransformTemplate,
            "vertex transform '" + request.vertex_transform.debug_name +
                "' has no resolved vertex source"));
        return result;
    }

    if (request.pass.uses_material_fragment &&
        (!request.material.shader.is_valid() ||
         request.material.shader.fragment_source()[0] == '\0')) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::MissingFragmentSource,
            "material fragment source is empty"));
        return result;
    }

    validate_fragment_interface(
        request.material,
        request.vertex_transform,
        result.diagnostics);

    std::vector<MaterialPipelineResourceDecl> resource_decls;
    append_resources(resource_decls, request.material.resources);
    append_resources(resource_decls, request.vertex_transform.resources);
    append_resources(resource_decls, request.pass.resources);

    MaterialPipelineResourceMergeResult merged =
        material_pipeline_merge_resources(resource_decls);
    result.diagnostics.insert(
        result.diagnostics.end(),
        merged.diagnostics.begin(),
        merged.diagnostics.end());
    if (!result.diagnostics.empty()) {
        return result;
    }

    std::vector<tc_shader_resource_requirement> requirements;
    requirements.reserve(merged.resources.size());
    for (const MaterialPipelineResourceDecl& resource : merged.resources) {
        requirements.push_back(resource_requirement_from_decl(resource));
    }
    apply_instance_stream_strides(request.vertex_transform, requirements);

    std::vector<tc_shader_contract_vertex_input> vertex_inputs;
    append_contract_inputs(request.vertex_transform, vertex_inputs);

    const std::string shader_name = default_shader_name(request);
    const std::string fragment_source = request.pass.uses_material_fragment
        ? request.material.shader.fragment_source()
        : request.pass.fragment_source_override;
    const std::string fragment_entry = request.pass.uses_material_fragment
        ? (request.material.shader.get() && request.material.shader.get()->fragment_entry
            ? request.material.shader.get()->fragment_entry
            : "main")
        : request.pass.fragment_entry_override;
    const std::string vertex_entry = request.vertex_entry_override.empty()
        ? request.vertex_transform.vertex_entry
        : request.vertex_entry_override;

    tc_shader_language language = request.language;
    tc_shader_artifact_policy artifact_policy = request.artifact_policy;
    if (request.material.shader.is_valid()) {
        language = request.material.shader.language();
        artifact_policy = request.material.shader.artifact_policy();
    }

    const tc_shader_create_desc shader_desc = {
        {
            vertex_source.c_str(),
            fragment_source.c_str(),
            request.geometry_source_override.empty()
                ? nullptr
                : request.geometry_source_override.c_str(),
            shader_name.c_str(),
            nullptr,
            vertex_entry.empty() ? nullptr : vertex_entry.c_str(),
            fragment_entry.empty() ? nullptr : fragment_entry.c_str(),
            request.geometry_entry_override.empty()
                ? nullptr
                : request.geometry_entry_override.c_str()
        },
        request.shader_uuid.empty() ? nullptr : request.shader_uuid.c_str(),
        language,
        artifact_policy
    };
    tc_shader_handle handle = tc_shader_from_sources_desc(&shader_desc);
    if (tc_shader_handle_is_invalid(handle)) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::ShaderCreationFailed,
            "tc_shader_from_sources_desc failed for '" +
                shader_name + "'"));
        return result;
    }

    tc_shader* shader = tc_shader_get(handle);
    if (!shader) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::ShaderCreationFailed,
            "tc_shader_get failed after creating '" + shader_name + "'"));
        return result;
    }
    if (request.material.shader.is_valid()) {
        shader->features = request.material.shader.get()->features;
    }

    tc_shader_contract_desc contract_desc{};
    contract_desc.schema_version = TC_SHADER_CONTRACT_SCHEMA_VERSION;
    contract_desc.source_kind = TC_SHADER_CONTRACT_SOURCE_ASSEMBLED;
    contract_desc.vertex_inputs = vertex_inputs.empty() ? nullptr : vertex_inputs.data();
    contract_desc.vertex_input_count = static_cast<uint32_t>(vertex_inputs.size());
    contract_desc.resources = requirements.empty() ? nullptr : requirements.data();
    contract_desc.resource_count = static_cast<uint32_t>(requirements.size());
    contract_desc.debug_name = shader_name.c_str();
    contract_desc.source_debug_name = "material_pipeline_assembler";
    if (!tc_shader_set_contract(shader, &contract_desc)) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::ShaderCreationFailed,
            "tc_shader_set_contract failed for '" + shader_name + "'"));
        return result;
    }

    result.shader = TcShader(handle);
    return result;
}

} // namespace termin
