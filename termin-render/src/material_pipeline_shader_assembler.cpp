#include <termin/render/material_pipeline_shader_assembler.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include <tcbase/tc_log.hpp>

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
    MaterialPipelineResourceOwner owner,
    uint32_t stage_mask)
{
    MaterialPipelineResourceDecl result{};
    result.requirement.name = binding.name;
    result.requirement.kind = binding.kind;
    result.requirement.scope = binding.scope;
    result.requirement.stage_mask = stage_mask;
    result.requirement.size = binding.size;
    result.owner = owner;
    return result;
}

MaterialPipelineResourceDecl resource_decl_from_requirement(
    const tc_shader_resource_requirement& requirement,
    MaterialPipelineResourceOwner owner)
{
    MaterialPipelineResourceDecl result{};
    result.requirement.name = requirement.name;
    result.requirement.kind = requirement.kind;
    result.requirement.scope = requirement.scope;
    result.requirement.stage_mask = requirement.stage_mask;
    result.requirement.size = requirement.size;
    result.owner = owner;
    return result;
}

std::optional<MaterialPipelineValueType> material_pipeline_value_type(
    uint32_t type)
{
    switch (type) {
    case TC_SHADER_CONTRACT_VALUE_FLOAT:
        return MaterialPipelineValueType::Float;
    case TC_SHADER_CONTRACT_VALUE_FLOAT2:
        return MaterialPipelineValueType::Float2;
    case TC_SHADER_CONTRACT_VALUE_FLOAT3:
        return MaterialPipelineValueType::Float3;
    case TC_SHADER_CONTRACT_VALUE_FLOAT4:
        return MaterialPipelineValueType::Float4;
    case TC_SHADER_CONTRACT_VALUE_MATRIX4:
        return MaterialPipelineValueType::Matrix4;
    default:
        return std::nullopt;
    }
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
    const MaterialFragmentInterface& fragment_input,
    const char* input_owner,
    const VertexTransformContract& vertex_transform,
    std::vector<MaterialPipelineDiagnostic>& diagnostics)
{
    bool ok = true;
    for (const MaterialPipelineSemantic& semantic :
         fragment_input.semantics) {
        if (!material_pipeline_interface_produces(
                vertex_transform.produced_fragment_input,
                semantic.name,
                semantic.type)) {
            ok = false;
            diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic,
                "vertex transform '" + vertex_transform.debug_name +
                    "' does not produce " + input_owner + " fragment semantic '" +
                    semantic.name + "'"));
        }
    }
    return ok;
}

bool validate_adapter_interface(
    const VertexTransformProvider& provider,
    const VertexOutputAdapter& adapter,
    std::vector<MaterialPipelineDiagnostic>& diagnostics)
{
    bool ok = true;
    for (const MaterialPipelineSemantic& semantic :
         adapter.consumed_world_semantics.semantics) {
        if (!material_pipeline_interface_produces(
                provider.produced_world_semantics,
                semantic.name,
                semantic.type)) {
            ok = false;
            diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic,
                "vertex transform provider '" + provider.debug_name +
                    "' does not produce adapter semantic '" +
                    semantic.name + "' required by '" + adapter.debug_name +
                    "'"));
        }
    }
    return ok;
}

std::string compose_modular_vertex_source(
    const VertexTransformProvider& provider,
    const VertexOutputAdapter& adapter,
    const std::string& entry_point)
{
    std::string source;
    source.reserve(
        provider.source_module.module_name.size() +
        adapter.source_module.module_name.size() +
        provider.entry_input_declaration.size() +
        provider.adapter_input_expression.size() + 320u);
    source += "// Termin material pipeline vertex glue.\n";
    source += "// provider=";
    source += provider.source_module.source_identity;
    source += "\n// adapter=";
    source += adapter.source_module.source_identity;
    source += "\nimport ";
    source += provider.source_module.module_name;
    source += ";\nimport ";
    source += adapter.source_module.module_name;
    source += ";\n\n";
    source += provider.entry_input_declaration;
    source += "\n\n[shader(\"vertex\")]\n";
    source += adapter.output_type_name;
    source += " ";
    source += entry_point;
    source += "(VertexInput input";
    source += adapter.entry_extra_parameters;
    source += ") {\n    return ";
    source += adapter.output_function;
    source += "(";
    source += provider.adapter_input_expression;
    source += adapter.output_extra_arguments;
    source += ");\n}\n";
    return source;
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

bool shader_identifier_character(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

bool source_defines_entry(
    const std::string& source,
    const std::string& entry)
{
    if (source.empty() || entry.empty()) {
        return false;
    }
    size_t position = 0;
    while ((position = source.find(entry, position)) != std::string::npos) {
        const bool has_identifier_before =
            position > 0 && shader_identifier_character(source[position - 1]);
        size_t after = position + entry.size();
        const bool has_identifier_after =
            after < source.size() && shader_identifier_character(source[after]);
        if (!has_identifier_before && !has_identifier_after) {
            while (after < source.size() &&
                   std::isspace(static_cast<unsigned char>(source[after])) != 0) {
                ++after;
            }
            if (after < source.size() && source[after] == '(') {
                return true;
            }
        }
        position += entry.size();
    }
    return false;
}

std::string compose_surface_fragment_source(
    const tc_surface_contract_desc& surface,
    const tc_shader_surface_producer_view& producer,
    const MaterialSurfaceConsumerContract& consumer)
{
    std::string source;
    source.reserve(
        std::char_traits<char>::length(surface.interface_source) +
        std::char_traits<char>::length(producer.evaluator_source) +
        consumer.consumer_source.size() + 256u);
    source += "// Termin surface contract: ";
    source += surface.source_identity;
    source += "\n";
    source += surface.interface_source;
    source += "\n\n// Termin material evaluator: ";
    source += producer.source_identity;
    source += "\n";
    source += producer.evaluator_source;
    source += "\n\n// Termin pass consumer: ";
    source += consumer.source_identity;
    source += "\n";
    source += consumer.consumer_source;
    source += "\n";
    return source;
}

} // namespace

MaterialPipelineMaterialContract material_pipeline_material_contract_from_shader(
    TcShader shader,
    MaterialFragmentInterface final_color_required_fragment_input)
{
    MaterialPipelineMaterialContract contract;
    contract.shader = shader;
    contract.required_fragment_input =
        std::move(final_color_required_fragment_input);

    if (tc_shader* raw = shader.get()) {
        tc_shader_surface_producer_view producer{};
        if (tc_shader_get_surface_producer_view(raw, &producer)) {
            contract.required_fragment_input.semantics.clear();
            contract.required_fragment_input.semantics.reserve(
                producer.fragment_input_count);
            for (uint32_t i = 0; i < producer.fragment_input_count; ++i) {
                const std::optional<MaterialPipelineValueType> type =
                    material_pipeline_value_type(producer.fragment_inputs[i].type);
                if (!type.has_value()) {
                    tc::Log::error(
                        "Surface producer '%s@%u' has unsupported fragment input type %u for '%s'",
                        producer.contract_id,
                        producer.contract_version,
                        producer.fragment_inputs[i].type,
                        producer.fragment_inputs[i].semantic);
                    continue;
                }
                contract.required_fragment_input.semantics.push_back({
                    producer.fragment_inputs[i].semantic,
                    *type,
                });
            }
            contract.resources.reserve(producer.resource_count);
            for (uint32_t i = 0; i < producer.resource_count; ++i) {
                contract.resources.push_back(resource_decl_from_requirement(
                    producer.resources[i],
                    MaterialPipelineResourceOwner::Material));
            }
            return contract;
        }

        const uint32_t count = tc_shader_resource_binding_count(raw);
        const tc_shader_resource_binding* bindings =
            tc_shader_resource_bindings(raw);
        contract.resources.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t fragment_stage_mask =
                bindings[i].stage_mask & TC_SHADER_STAGE_FRAGMENT;
            if (fragment_stage_mask == 0u) {
                continue;
            }
            contract.resources.push_back(resource_decl_from_binding(
                bindings[i],
                MaterialPipelineResourceOwner::Material,
                fragment_stage_mask));
        }
    }

    return contract;
}

MaterialPipelineShaderAssemblyResult material_pipeline_assemble_shader(
    const MaterialPipelineShaderAssemblyRequest& request)
{
    MaterialPipelineShaderAssemblyResult result;

    const bool has_modular_provider =
        vertex_transform_provider_is_modular(request.vertex_transform);
    const bool has_output_adapter = request.pass.vertex_output_adapter.has_value();

    std::string vertex_source = request.vertex_source_override;
    if (vertex_source.empty() && has_modular_provider && has_output_adapter) {
        const std::string& entry = request.vertex_entry_override.empty()
            ? request.vertex_transform.vertex_entry
            : request.vertex_entry_override;
        vertex_source = compose_modular_vertex_source(
            request.vertex_transform,
            *request.pass.vertex_output_adapter,
            entry);
    }
    if (vertex_source.empty()) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::MissingVertexTransformTemplate,
            "vertex transform '" + request.vertex_transform.debug_name +
                "' has no resolved vertex source"));
        return result;
    }

    std::string fragment_source;
    std::string fragment_entry;
    std::vector<MaterialPipelineResourceDecl> fragment_resources;
    MaterialFragmentComposition fragment_composition =
        request.pass.fragment_composition;
    if (fragment_composition ==
        MaterialFragmentComposition::SurfaceConsumerOrFinalColor) {
        fragment_composition = request.material.shader.has_surface_producer()
            ? MaterialFragmentComposition::SurfaceConsumer
            : MaterialFragmentComposition::FinalColor;
    }
    switch (fragment_composition) {
    case MaterialFragmentComposition::FinalColor: {
        if (!request.material.shader.is_valid() ||
            request.material.shader.fragment_source()[0] == '\0') {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentSource,
                "final-color material fragment source is empty"));
            break;
        }
        if (!request.material.shader.is_executable()) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::FragmentCompositionMismatch,
                "pass '" + request.pass.debug_name +
                    "' requires a final-color fragment but the material is a "
                    "surface producer"));
            break;
        }
        fragment_source = request.material.shader.fragment_source();
        const tc_shader* raw = request.material.shader.get();
        fragment_entry = raw && raw->fragment_entry
            ? raw->fragment_entry
            : "main";
        if (!source_defines_entry(fragment_source, fragment_entry)) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentEntry,
                "final-color entry '" + fragment_entry +
                    "' was not found in material fragment source"));
        }
        validate_fragment_interface(
            request.material.required_fragment_input,
            "material",
            request.vertex_transform,
            result.diagnostics);
        fragment_resources = request.material.resources;
        break;
    }
    case MaterialFragmentComposition::SurfaceConsumer: {
        if (!request.material.shader.is_valid()) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentSource,
                "surface material shader is missing"));
            break;
        }
        if (request.material.shader.language() != TC_SHADER_LANGUAGE_SLANG) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::FragmentCompositionMismatch,
                "surface producer/consumer composition requires Slang source"));
            break;
        }
        tc_shader_surface_producer_view producer{};
        if (!tc_shader_get_surface_producer_view(
                request.material.shader.get(),
                &producer)) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::FragmentCompositionMismatch,
                "pass '" + request.pass.debug_name +
                    "' requires a surface producer but the material is final-color"));
            break;
        }
        if (!request.pass.surface_consumer.has_value()) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentSource,
                "pass '" + request.pass.debug_name +
                    "' selected SurfaceConsumer without a consumer descriptor"));
            break;
        }
        const MaterialSurfaceConsumerContract& consumer =
            *request.pass.surface_consumer;
        if (consumer.accepted_surface.id != producer.contract_id ||
            consumer.accepted_surface.version != producer.contract_version) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::SurfaceContractMismatch,
                "pass '" + request.pass.debug_name + "' accepts '" +
                    consumer.accepted_surface.id + "@" +
                    std::to_string(consumer.accepted_surface.version) +
                    "' but material produces '" + producer.contract_id + "@" +
                    std::to_string(producer.contract_version) + "'"));
            break;
        }
        const tc_surface_contract_desc* surface =
            tc_surface_contract_registry_find({
                producer.contract_id,
                producer.contract_version,
            });
        if (!surface) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::UnknownSurfaceContract,
                "surface contract '" + std::string(producer.contract_id) + "@" +
                    std::to_string(producer.contract_version) +
                    "' is not registered"));
            break;
        }
        if (std::string(surface->surface_type_name) !=
            producer.surface_type_name) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::SurfaceContractMismatch,
                "registered surface type '" +
                    std::string(surface->surface_type_name) +
                    "' does not match producer type '" +
                    producer.surface_type_name + "'"));
            break;
        }
        if (!source_defines_entry(
                producer.evaluator_source,
                producer.evaluator_entry)) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentEntry,
                "surface evaluator entry '" +
                    std::string(producer.evaluator_entry) +
                    "' was not found in evaluator source"));
        }
        if (!source_defines_entry(
                consumer.consumer_source,
                consumer.fragment_entry)) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentEntry,
                "surface consumer entry '" + consumer.fragment_entry +
                    "' was not found in consumer source"));
        }
        if (consumer.source_identity.empty()) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentSource,
                "surface consumer source identity is empty"));
        }
        validate_fragment_interface(
            request.material.required_fragment_input,
            "surface producer",
            request.vertex_transform,
            result.diagnostics);
        validate_fragment_interface(
            consumer.required_fragment_input,
            "surface consumer",
            request.vertex_transform,
            result.diagnostics);
        fragment_source = compose_surface_fragment_source(
            *surface,
            producer,
            consumer);
        fragment_entry = consumer.fragment_entry;
        fragment_resources = request.material.resources;
        append_resources(fragment_resources, consumer.resources);
        break;
    }
    case MaterialFragmentComposition::PassOwned:
        if (request.pass.fragment_source_override.empty()) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentSource,
                "pass '" + request.pass.debug_name +
                    "' selected PassOwned without a fragment source"));
            break;
        }
        fragment_source = request.pass.fragment_source_override;
        fragment_entry = request.pass.fragment_entry_override;
        if (!source_defines_entry(fragment_source, fragment_entry)) {
            result.diagnostics.push_back(diagnostic(
                MaterialPipelineDiagnosticCode::MissingFragmentEntry,
                "pass-owned entry '" + fragment_entry +
                    "' was not found in fragment source"));
        }
        validate_fragment_interface(
            request.pass.required_material_fragment_input,
            "pass-owned",
            request.vertex_transform,
            result.diagnostics);
        break;
    case MaterialFragmentComposition::SurfaceConsumerOrFinalColor:
        break;
    }

    if (has_modular_provider != has_output_adapter) {
        result.diagnostics.push_back(diagnostic(
            MaterialPipelineDiagnosticCode::MissingVertexTransformTemplate,
            "pass '" + request.pass.debug_name +
                "' must provide both a modular vertex transform provider and "
                "a vertex output adapter"));
    }
    if (has_modular_provider && has_output_adapter) {
        validate_adapter_interface(
            request.vertex_transform,
            *request.pass.vertex_output_adapter,
            result.diagnostics);
    }

    std::vector<MaterialPipelineResourceDecl> resource_decls;
    append_resources(resource_decls, fragment_resources);
    append_resources(resource_decls, request.vertex_transform.resources);
    if (has_output_adapter) {
        append_resources(resource_decls, request.pass.vertex_output_adapter->resources);
    }
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
        artifact_policy,
        nullptr
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
