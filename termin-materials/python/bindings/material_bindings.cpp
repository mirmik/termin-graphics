#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/optional.h>
#include <tgfx/tgfx_material_handle.hpp>
#include "termin/materials/shader_parser.hpp"
#include "tgfx/render_state.hpp"
#include <tgfx/tgfx_shader_handle.hpp>
#include <tcbase/tc_log.hpp>
#include <cstring>
#include <initializer_list>
extern "C" {
#include <tgfx/resources/tc_shader_abi.h>
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_material_registry.h>
}

namespace termin {

namespace nb = nanobind;

namespace {

constexpr uint32_t GLSL_MATERIAL_BINDING = 1;
constexpr uint32_t GLSL_PER_FRAME_BINDING = 2;
constexpr uint32_t GLSL_DRAW_DATA_BINDING = 24;
constexpr uint32_t GLSL_MATERIAL_TEXTURE_BINDING_BASE = 4;
constexpr uint32_t STAGE_ALL_GRAPHICS =
    TC_SHADER_STAGE_VERTEX | TC_SHADER_STAGE_FRAGMENT | TC_SHADER_STAGE_GEOMETRY;

bool source_contains_any(
    const std::string& source,
    std::initializer_list<const char*> tokens)
{
    for (const char* token : tokens) {
        if (source.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

ShaderPhase infer_raw_glsl_resource_phase(
    const std::string& vertex_source,
    const std::string& fragment_source,
    const std::string& geometry_source)
{
    ShaderPhase shader_phase;
    const bool uses_per_frame =
        source_contains_any(vertex_source, {"uniform PerFrame", "ConstantBuffer<PerFrame>", "u_per_frame"}) ||
        source_contains_any(fragment_source, {"uniform PerFrame", "ConstantBuffer<PerFrame>", "u_per_frame"}) ||
        source_contains_any(geometry_source, {"uniform PerFrame", "ConstantBuffer<PerFrame>", "u_per_frame"});
    const bool uses_draw_data =
        source_contains_any(vertex_source, {"uniform DrawData", "ConstantBuffer<DrawData>", "draw_data"}) ||
        source_contains_any(fragment_source, {"uniform DrawData", "ConstantBuffer<DrawData>", "draw_data"}) ||
        source_contains_any(geometry_source, {"uniform DrawData", "ConstantBuffer<DrawData>", "draw_data"});

    shader_phase.uses_engine_per_frame = uses_per_frame;
    shader_phase.uses_engine_draw_data = uses_draw_data;
    return shader_phase;
}

TcTexture require_tc_texture(nb::object value, const std::string& context) {
    if (nb::isinstance<TcTexture>(value)) {
        return nb::cast<TcTexture>(value);
    }
    tc::Log::error("%s expects TcTexture; asset-layer lookups must resolve to TcTexture", context.c_str());
    throw std::runtime_error(context + " expects TcTexture");
}

TcTexture optional_tc_texture(nb::object value, const std::string& context) {
    if (value.is_none()) {
        return TcTexture();
    }
    return require_tc_texture(value, context);
}

bool supports_python_buffer(nb::handle value) {
    return PyObject_CheckBuffer(value.ptr()) != 0;
}

tc_shader_language shader_language_from_string(const std::string& language) {
    if (language == "glsl") {
        return TC_SHADER_LANGUAGE_GLSL;
    }
    if (language == "slang") {
        return TC_SHADER_LANGUAGE_SLANG;
    }
    if (language == "hlsl") {
        return TC_SHADER_LANGUAGE_HLSL;
    }
    tc::Log::error("Unsupported shader language '%s'", language.c_str());
    throw std::runtime_error("Unsupported shader language: " + language);
}

tc_shader_artifact_policy artifact_policy_for_language(tc_shader_language language) {
    return language == TC_SHADER_LANGUAGE_SLANG
        ? TC_SHADER_ARTIFACT_REQUIRED
        : TC_SHADER_ARTIFACT_OPTIONAL;
}

void put_uniform_value(nb::dict& result, const std::string& name, tc_uniform_value& u) {
    switch (u.type) {
        case TC_UNIFORM_BOOL:
        case TC_UNIFORM_INT:
            result[nb::cast(name)] = u.data.i;
            break;
        case TC_UNIFORM_FLOAT:
            result[nb::cast(name)] = u.data.f;
            break;
        case TC_UNIFORM_VEC2:
            result[nb::cast(name)] = nb::make_tuple(u.data.v2[0], u.data.v2[1]);
            break;
        case TC_UNIFORM_VEC3:
            result[nb::cast(name)] = Vec3{u.data.v3[0], u.data.v3[1], u.data.v3[2]};
            break;
        case TC_UNIFORM_VEC4:
            result[nb::cast(name)] = Vec4{u.data.v4[0], u.data.v4[1], u.data.v4[2], u.data.v4[3]};
            break;
        default:
            break;
    }
}

void append_resource_binding(
    std::vector<tc_shader_resource_binding>& bindings,
    const std::string& name,
    uint32_t kind,
    uint32_t scope,
    uint32_t binding_index,
    uint32_t size = 0)
{
    tc_shader_resource_binding binding{};
    std::strncpy(binding.name, name.c_str(), TC_SHADER_RESOURCE_NAME_MAX - 1);
    binding.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
    binding.kind = kind;
    binding.scope = scope;
    binding.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    binding.binding = binding_index;
    binding.stage_mask = STAGE_ALL_GRAPHICS;
    binding.size = size;
    bindings.push_back(binding);
}

const tc_shader_abi_resource_decl& require_shader_abi_resource(
    uint32_t id,
    const char* context)
{
    const tc_shader_abi_resource_decl* abi = tc_shader_abi_resource(id);
    if (!abi) {
        tc::Log::error("%s references unknown shader ABI resource id %u", context, id);
        throw std::runtime_error("Unknown shader ABI resource id");
    }
    return *abi;
}

void append_abi_resource_binding(
    std::vector<tc_shader_resource_binding>& bindings,
    uint32_t id,
    uint32_t binding_index,
    uint32_t size = 0)
{
    const tc_shader_abi_resource_decl& abi =
        require_shader_abi_resource(id, "append_abi_resource_binding");
    append_resource_binding(
        bindings,
        abi.canonical_name,
        abi.kind,
        abi.scope,
        binding_index,
        size);
}

bool contract_has_vertex_input(
    const std::vector<tc_shader_contract_vertex_input>& inputs,
    const char* semantic)
{
    for (const auto& input : inputs) {
        if (std::strncmp(input.semantic, semantic, TC_SHADER_RESOURCE_NAME_MAX) == 0) {
            return true;
        }
    }
    return false;
}

void append_contract_vertex_input(
    std::vector<tc_shader_contract_vertex_input>& inputs,
    const char* semantic,
    uint32_t type)
{
    if (contract_has_vertex_input(inputs, semantic)) {
        return;
    }

    tc_shader_contract_vertex_input input{};
    std::strncpy(input.semantic, semantic, TC_SHADER_RESOURCE_NAME_MAX - 1);
    input.semantic[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
    input.type = type;
    input.required = 1;
    inputs.push_back(input);
}

std::vector<tc_shader_contract_vertex_input> infer_material_vertex_contract(
    const ShaderPhase& shader_phase)
{
    std::vector<tc_shader_contract_vertex_input> inputs;
    auto it = shader_phase.stages.find("vertex");
    if (it == shader_phase.stages.end()) {
        return inputs;
    }

    const std::string& source = it->second.source;
    if (source_contains_any(source, {": POSITION", ":POSITION", "a_position", "in_position"})) {
        append_contract_vertex_input(inputs, "position", TC_SHADER_CONTRACT_VALUE_FLOAT3);
    }
    if (source_contains_any(source, {": NORMAL", ":NORMAL", "a_normal", "in_normal"})) {
        append_contract_vertex_input(inputs, "normal", TC_SHADER_CONTRACT_VALUE_FLOAT3);
    }
    if (source_contains_any(
            source,
            {": TEXCOORD0", ":TEXCOORD0", ": TEXCOORD", ":TEXCOORD", "a_uv", "in_uv", "a_texcoord"})) {
        append_contract_vertex_input(inputs, "uv", TC_SHADER_CONTRACT_VALUE_FLOAT2);
    }
    if (source_contains_any(source, {": TANGENT", ":TANGENT", "a_tangent", "in_tangent"})) {
        append_contract_vertex_input(inputs, "tangent", TC_SHADER_CONTRACT_VALUE_FLOAT4);
    }
    if (source_contains_any(source, {": BLENDINDICES", ":BLENDINDICES", "a_joints", "in_joints"})) {
        append_contract_vertex_input(inputs, "joints", TC_SHADER_CONTRACT_VALUE_FLOAT4);
    }
    if (source_contains_any(source, {": BLENDWEIGHT", ":BLENDWEIGHT", "a_weights", "in_weights"})) {
        append_contract_vertex_input(inputs, "weights", TC_SHADER_CONTRACT_VALUE_FLOAT4);
    }
    return inputs;
}

struct ResourceRequirementInfo {
    std::string name;
    uint32_t kind = 0;
    uint32_t scope = 0;
    uint32_t stage_mask = 0;
    uint32_t size = 0;
    const tc_shader_resource_field* fields = nullptr;
    uint32_t field_count = 0;
};

void append_resource_requirement(
    std::vector<tc_shader_resource_requirement>& requirements,
    const ResourceRequirementInfo& info)
{
    tc_shader_resource_requirement requirement{};
    std::strncpy(requirement.name, info.name.c_str(), TC_SHADER_RESOURCE_NAME_MAX - 1);
    requirement.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
    requirement.kind = info.kind;
    requirement.scope = info.scope;
    requirement.stage_mask = info.stage_mask;
    requirement.size = info.size;
    requirement.element_stride = 0;
    requirement.fields = const_cast<tc_shader_resource_field*>(info.fields);
    requirement.field_count = info.field_count;
    requirements.push_back(requirement);
}

void append_abi_resource_requirement(
    std::vector<tc_shader_resource_requirement>& requirements,
    uint32_t id,
    uint32_t stage_mask,
    uint32_t size = 0,
    const tc_shader_resource_field* fields = nullptr,
    uint32_t field_count = 0)
{
    const tc_shader_abi_resource_decl& abi =
        require_shader_abi_resource(id, "append_abi_resource_requirement");
    ResourceRequirementInfo info;
    info.name = abi.canonical_name;
    info.kind = abi.kind;
    info.scope = abi.scope;
    info.stage_mask = stage_mask;
    info.size = size;
    info.fields = fields;
    info.field_count = field_count;
    append_resource_requirement(requirements, info);
}

std::vector<tc_shader_resource_field> material_fields_from_layout(
    const MaterialUboLayout& layout)
{
    std::vector<tc_shader_resource_field> fields;
    fields.reserve(layout.entries.size());
    for (const MaterialUboEntry& entry : layout.entries) {
        tc_shader_resource_field field{};
        std::strncpy(field.name, entry.name.c_str(), TC_SHADER_RESOURCE_NAME_MAX - 1);
        field.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
        std::strncpy(field.type, entry.property_type.c_str(), TC_MATERIAL_UBO_TYPE_MAX - 1);
        field.type[TC_MATERIAL_UBO_TYPE_MAX - 1] = '\0';
        field.offset = entry.offset;
        field.size = entry.size;
        fields.push_back(field);
    }
    return fields;
}

std::vector<tc_shader_resource_requirement> parser_shader_resource_requirements(
    const ShaderPhase& shader_phase,
    const MaterialUboLayout& layout,
    const std::vector<tc_shader_resource_field>& material_fields)
{
    std::vector<tc_shader_resource_requirement> requirements;
    requirements.reserve(
        (layout.empty() ? 0u : 1u) +
        (shader_phase.uses_engine_per_frame ? 1u : 0u) +
        (shader_phase.uses_engine_draw_data ? 1u : 0u) +
        shader_phase.material_texture_resources.size());

    if (!layout.empty()) {
        append_abi_resource_requirement(
            requirements,
            TC_SHADER_ABI_RESOURCE_MATERIAL,
            STAGE_ALL_GRAPHICS,
            layout.block_size,
            material_fields.empty() ? nullptr : material_fields.data(),
            static_cast<uint32_t>(material_fields.size()));
    }
    if (shader_phase.uses_engine_per_frame) {
        append_abi_resource_requirement(
            requirements,
            TC_SHADER_ABI_RESOURCE_PER_FRAME,
            STAGE_ALL_GRAPHICS);
    }
    if (shader_phase.uses_engine_draw_data) {
        append_abi_resource_requirement(
            requirements,
            TC_SHADER_ABI_RESOURCE_DRAW_DATA,
            STAGE_ALL_GRAPHICS,
            64);
    }
    for (const std::string& texture_name : shader_phase.material_texture_resources) {
        ResourceRequirementInfo info;
        info.name = texture_name;
        info.kind = TC_SHADER_RESOURCE_TEXTURE;
        info.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
        info.stage_mask = STAGE_ALL_GRAPHICS;
        append_resource_requirement(requirements, info);
    }

    return requirements;
}

void apply_parser_shader_contract(
    tc_shader* shader,
    const ShaderPhase& shader_phase,
    const MaterialUboLayout& layout)
{
    if (!shader) {
        tc::Log::error("apply_parser_shader_contract called with null shader");
        return;
    }

    std::vector<tc_shader_contract_vertex_input> vertex_inputs =
        infer_material_vertex_contract(shader_phase);
    std::vector<tc_shader_resource_field> material_fields =
        material_fields_from_layout(layout);
    std::vector<tc_shader_resource_requirement> resources =
        parser_shader_resource_requirements(
            shader_phase,
            layout,
            material_fields);

    tc_shader_contract_desc desc{};
    desc.schema_version = TC_SHADER_CONTRACT_SCHEMA_VERSION;
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_DECLARED;
    desc.vertex_inputs = vertex_inputs.empty() ? nullptr : vertex_inputs.data();
    desc.vertex_input_count = static_cast<uint32_t>(vertex_inputs.size());
    desc.resources = resources.empty() ? nullptr : resources.data();
    desc.resource_count = static_cast<uint32_t>(resources.size());
    desc.debug_name = shader->name ? shader->name : shader->uuid;
    desc.source_debug_name = "termin-materials shader parser";

    if (!tc_shader_set_contract(shader, &desc)) {
        tc::Log::error(
            "Failed to attach shader parser contract to material shader '%s'",
            shader->name ? shader->name : shader->uuid);
    }
}

void apply_parser_resource_layout(
    tc_shader* shader,
    const ShaderPhase& shader_phase,
    const MaterialUboLayout& layout,
    tc_shader_language language)
{
    std::vector<tc_shader_resource_binding> bindings;
    if (!layout.empty()) {
        std::vector<tc_material_ubo_entry> entries;
        entries.reserve(layout.entries.size());
        for (const auto& src : layout.entries) {
            tc_material_ubo_entry entry{};
            std::strncpy(entry.name, src.name.c_str(), TC_MATERIAL_UBO_NAME_MAX - 1);
            entry.name[TC_MATERIAL_UBO_NAME_MAX - 1] = '\0';
            std::strncpy(
                entry.property_type,
                src.property_type.c_str(),
                TC_MATERIAL_UBO_TYPE_MAX - 1);
            entry.property_type[TC_MATERIAL_UBO_TYPE_MAX - 1] = '\0';
            entry.offset = src.offset;
            entry.size = src.size;
            entries.push_back(entry);
        }
        tc_shader_set_material_ubo_layout(
            shader,
            entries.data(),
            static_cast<uint32_t>(entries.size()),
            layout.block_size);
    } else {
        tc_shader_set_material_ubo_layout(shader, nullptr, 0, 0);
    }

    if (language == TC_SHADER_LANGUAGE_GLSL) {
        if (!layout.empty()) {
            append_abi_resource_binding(
                bindings,
                TC_SHADER_ABI_RESOURCE_MATERIAL,
                GLSL_MATERIAL_BINDING,
                layout.block_size);
        }
        if (shader_phase.uses_engine_per_frame) {
            append_abi_resource_binding(
                bindings,
                TC_SHADER_ABI_RESOURCE_PER_FRAME,
                GLSL_PER_FRAME_BINDING);
        }
        if (shader_phase.uses_engine_draw_data) {
            append_abi_resource_binding(
                bindings,
                TC_SHADER_ABI_RESOURCE_DRAW_DATA,
                GLSL_DRAW_DATA_BINDING,
                64);
        }
        for (size_t i = 0; i < shader_phase.material_texture_resources.size(); ++i) {
            append_resource_binding(
                bindings,
                shader_phase.material_texture_resources[i],
                TC_SHADER_RESOURCE_TEXTURE,
                TC_SHADER_RESOURCE_SCOPE_MATERIAL,
                GLSL_MATERIAL_TEXTURE_BINDING_BASE + static_cast<uint32_t>(i));
        }
    }

    if (language == TC_SHADER_LANGUAGE_GLSL) {
        tc_shader_set_resource_layout(
            shader,
            bindings.empty() ? nullptr : bindings.data(),
            static_cast<uint32_t>(bindings.size()));
    }
    apply_parser_shader_contract(shader, shader_phase, layout);
}

struct ParsedMaterialCreateOptions {
    nb::object color = nb::none();
    nb::object textures = nb::none();
    nb::object uniforms = nb::none();
    nb::object name = nb::none();
    nb::object source_path = nb::none();
    std::string shader_uuid;
    nb::object default_white_texture = nb::none();
    nb::object default_normal_texture = nb::none();
};

TcMaterial create_material_from_parsed(
    const ShaderMultyPhaseProgramm& program,
    const ParsedMaterialCreateOptions& options)
{
    if (program.phases.empty()) {
        throw std::runtime_error("Program has no phases");
    }
    tc_shader_language language = shader_language_from_string(program.language);

    // Create material with uuid hint if provided
    TcMaterial mat = TcMaterial::create(
        options.name.is_none() ? program.program : nb::cast<std::string>(options.name),
        options.shader_uuid
    );
    if (!mat.is_valid()) {
        throw std::runtime_error("Failed to create TcMaterial");
    }

    mat.set_shader_name(program.program.c_str());
    if (!options.source_path.is_none()) {
        mat.set_source_path(nb::cast<std::string>(options.source_path).c_str());
    }

    TcTexture white_tex = optional_tc_texture(
        options.default_white_texture,
        "create_material_from_parsed(default_white_texture)");
    TcTexture normal_tex = optional_tc_texture(
        options.default_normal_texture,
        "create_material_from_parsed(default_normal_texture)");

    for (const auto& shader_phase : program.phases) {
        // Get shader sources from stages
        auto it_vert = shader_phase.stages.find("vertex");
        auto it_frag = shader_phase.stages.find("fragment");
        auto it_geom = shader_phase.stages.find("geometry");

        if (it_vert == shader_phase.stages.end()) {
            throw std::runtime_error("Phase has no vertex stage");
        }
        if (it_frag == shader_phase.stages.end()) {
            throw std::runtime_error("Phase has no fragment stage");
        }

        std::string vs = it_vert->second.source;
        std::string fs = it_frag->second.source;
        std::string gs = (it_geom != shader_phase.stages.end()) ? it_geom->second.source : "";

        // Build render state
        tc_render_state rs = tc_render_state_opaque();
        rs.depth_write = shader_phase.gl_depth_mask.value_or(true);
        rs.depth_test = shader_phase.gl_depth_test.value_or(true);
        rs.blend = shader_phase.gl_blend.value_or(false);
        rs.cull = shader_phase.gl_cull.value_or(true);

        // Build shader name
        std::string shader_name;
        if (!program.program.empty()) {
            shader_name = program.program;
            if (!shader_phase.phase_mark.empty()) {
                shader_name += "/" + shader_phase.phase_mark;
            }
        } else if (!shader_phase.phase_mark.empty()) {
            shader_name = shader_phase.phase_mark;
        }

        TcMaterialPhaseFromSourcesInfo phase_info;
        phase_info.shader.sources.vertex = vs;
        phase_info.shader.sources.fragment = fs;
        phase_info.shader.sources.geometry = gs;
        phase_info.shader.sources.name = shader_name;
        phase_info.shader.sources.vertex_entry = it_vert->second.entry;
        phase_info.shader.sources.fragment_entry = it_frag->second.entry;
        if (it_geom != shader_phase.stages.end()) {
            phase_info.shader.sources.geometry_entry = it_geom->second.entry;
        }
        phase_info.shader.language = language;
        phase_info.shader.artifact_policy = artifact_policy_for_language(language);
        phase_info.phase_mark = shader_phase.phase_mark;
        phase_info.priority = shader_phase.priority;
        phase_info.state = rs;

        tc_material_phase* phase = mat.add_phase_from_sources(phase_info);

        if (!phase) {
            tc::Log::error("Failed to add phase '%s' to material", shader_phase.phase_mark.c_str());
            continue;
        }

        // Set available marks
        phase->available_mark_count = std::min(shader_phase.available_marks.size(), (size_t)TC_MATERIAL_MAX_MARKS);
        for (size_t i = 0; i < phase->available_mark_count; i++) {
            strncpy(phase->available_marks[i], shader_phase.available_marks[i].c_str(), TC_PHASE_MARK_MAX - 1);
            phase->available_marks[i][TC_PHASE_MARK_MAX - 1] = '\0';
        }

        // Apply shader features
        TcShader shader(phase->shader);
        for (const auto& feature : program.features) {
            if (feature == "lighting_ubo") {
                shader.set_feature(TC_SHADER_FEATURE_LIGHTING_UBO);
            }
        }

        const MaterialUboLayout& layout = shader_phase.material_ubo_layout;
        apply_parser_resource_layout(
            tc_shader_get(phase->shader),
            shader_phase,
            layout,
            language);

        std::vector<MaterialProperty> shader_uniforms = shader_phase.uniforms;
        shader_uniforms.insert(
            shader_uniforms.end(),
            shader_phase.material_uniforms.begin(),
            shader_phase.material_uniforms.end());

        // Apply uniforms from defaults
        for (const auto& prop : shader_uniforms) {
            if (std::holds_alternative<std::monostate>(prop.default_value)) continue;

            if (std::holds_alternative<bool>(prop.default_value)) {
                int val = std::get<bool>(prop.default_value) ? 1 : 0;
                tc_material_phase_set_uniform(phase, prop.name.c_str(), TC_UNIFORM_INT, &val);
            } else if (std::holds_alternative<int>(prop.default_value)) {
                int val = std::get<int>(prop.default_value);
                tc_material_phase_set_uniform(phase, prop.name.c_str(), TC_UNIFORM_INT, &val);
            } else if (std::holds_alternative<double>(prop.default_value)) {
                float val = static_cast<float>(std::get<double>(prop.default_value));
                tc_material_phase_set_uniform(phase, prop.name.c_str(), TC_UNIFORM_FLOAT, &val);
            } else if (std::holds_alternative<std::vector<double>>(prop.default_value)) {
                const auto& vec = std::get<std::vector<double>>(prop.default_value);
                if (vec.size() == 3) {
                    float arr[3] = {(float)vec[0], (float)vec[1], (float)vec[2]};
                    tc_material_phase_set_uniform(phase, prop.name.c_str(), TC_UNIFORM_VEC3, arr);
                } else if (vec.size() == 4) {
                    float arr[4] = {(float)vec[0], (float)vec[1], (float)vec[2], (float)vec[3]};
                    tc_material_phase_set_uniform(phase, prop.name.c_str(), TC_UNIFORM_VEC4, arr);
                }
            }
        }

        // Apply extra uniforms
        if (!options.uniforms.is_none()) {
            nb::dict extras = nb::cast<nb::dict>(options.uniforms);
            for (auto item : extras) {
                std::string key = nb::cast<std::string>(item.first);
                nb::object val = nb::borrow<nb::object>(item.second);
                if (nb::isinstance<nb::bool_>(val)) {
                    int v = nb::cast<bool>(val) ? 1 : 0;
                    tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_INT, &v);
                } else if (nb::isinstance<nb::int_>(val)) {
                    int v = nb::cast<int>(val);
                    tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_INT, &v);
                } else if (nb::isinstance<nb::float_>(val)) {
                    float v = nb::cast<float>(val);
                    tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_FLOAT, &v);
                } else if (nb::isinstance<Vec3>(val)) {
                    Vec3 v = nb::cast<Vec3>(val);
                    float arr[3] = {(float)v.x, (float)v.y, (float)v.z};
                    tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_VEC3, arr);
                } else if (nb::isinstance<Vec4>(val)) {
                    Vec4 v = nb::cast<Vec4>(val);
                    float arr[4] = {(float)v.x, (float)v.y, (float)v.z, (float)v.w};
                    tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_VEC4, arr);
                }
            }
        }

        // Set default textures
        for (const auto& prop : shader_uniforms) {
            if (prop.property_type == "Texture") {
                if (std::holds_alternative<std::string>(prop.default_value)) {
                    const std::string& default_tex_name = std::get<std::string>(prop.default_value);
                    if (default_tex_name == "normal") {
                        if (normal_tex.is_valid()) {
                            tc_material_phase_set_texture(phase, prop.name.c_str(), normal_tex.handle);
                        }
                    } else {
                        if (white_tex.is_valid()) {
                            tc_material_phase_set_texture(phase, prop.name.c_str(), white_tex.handle);
                        }
                    }
                } else {
                    if (white_tex.is_valid()) {
                        tc_material_phase_set_texture(phase, prop.name.c_str(), white_tex.handle);
                    }
                }
            }
        }

        // Override with provided textures
        if (!options.textures.is_none()) {
            nb::dict tex_dict = nb::cast<nb::dict>(options.textures);
            for (auto item : tex_dict) {
                std::string key = nb::cast<std::string>(item.first);
                nb::object val = nb::borrow<nb::object>(item.second);
                if (nb::isinstance<TcTexture>(val)) {
                    TcTexture tex = nb::cast<TcTexture>(val);
                    tc_material_phase_set_texture(phase, key.c_str(), tex.handle);
                } else {
                    TcTexture tex = require_tc_texture(val, "create_material_from_parsed(textures)");
                    tc_material_phase_set_texture(phase, key.c_str(), tex.handle);
                }
            }
        }

        // Set color
        if (!options.color.is_none()) {
            if (nb::isinstance<Vec4>(options.color)) {
                Vec4 c = nb::cast<Vec4>(options.color);
                tc_material_phase_set_color(phase, c.x, c.y, c.z, c.w);
            } else if (
                nb::isinstance<nb::tuple>(options.color) ||
                nb::isinstance<nb::list>(options.color)) {
                nb::sequence seq = nb::cast<nb::sequence>(options.color);
                tc_material_phase_set_color(phase,
                    nb::cast<float>(seq[0]),
                    nb::cast<float>(seq[1]),
                    nb::cast<float>(seq[2]),
                    nb::cast<float>(seq[3])
                );
            }
        }
    }

    return mat;
}

// NOLINTNEXTLINE(readability-function-size): preserves the Python API; implementation uses ParsedMaterialCreateOptions.
TcMaterial create_material_from_parsed_py(
    const ShaderMultyPhaseProgramm& program,
    nb::object color,
    nb::object textures,
    nb::object uniforms,
    nb::object name,
    nb::object source_path,
    const std::string& shader_uuid,
    nb::object default_white_texture,
    nb::object default_normal_texture)
{
    ParsedMaterialCreateOptions options;
    options.color = color;
    options.textures = textures;
    options.uniforms = uniforms;
    options.name = name;
    options.source_path = source_path;
    options.shader_uuid = shader_uuid;
    options.default_white_texture = default_white_texture;
    options.default_normal_texture = default_normal_texture;
    return create_material_from_parsed(program, options);
}

} // namespace

void bind_material(nb::module_& m) {
    // Old MaterialPhase and Material classes removed - use TcMaterialPhase and TcMaterial
}

void bind_tc_material(nb::module_& m) {
    // tc_render_state struct
    nb::class_<tc_render_state>(m, "TcRenderState")
        .def(nb::init<>())
        .def_rw("polygon_mode", &tc_render_state::polygon_mode)
        .def_rw("cull", &tc_render_state::cull)
        .def_rw("depth_test", &tc_render_state::depth_test)
        .def_rw("depth_write", &tc_render_state::depth_write)
        .def_rw("blend", &tc_render_state::blend)
        .def_rw("blend_src", &tc_render_state::blend_src)
        .def_rw("blend_dst", &tc_render_state::blend_dst)
        .def_rw("depth_func", &tc_render_state::depth_func)
        .def_static("opaque", &tc_render_state_opaque)
        .def_static("transparent", &tc_render_state_transparent)
        .def_static("wireframe", &tc_render_state_wireframe);

    // tc_material_phase struct (opaque pointer, limited access)
    nb::class_<tc_material_phase>(m, "TcMaterialPhase")
        .def_prop_ro("phase_mark", [](tc_material_phase& p) { return p.phase_mark; })
        .def_prop_ro("priority", [](tc_material_phase& p) { return p.priority; })
        .def_prop_ro("texture_count", [](tc_material_phase& p) { return p.texture_count; })
        .def_prop_ro("uniform_count", [](tc_material_phase& p) { return p.uniform_count; })
        .def_prop_ro("shader", [](tc_material_phase& p) { return TcShader(p.shader); })
        .def_prop_ro("textures", [](tc_material_phase& p) {
            nb::dict result;
            for (size_t i = 0; i < p.texture_count; i++) {
                std::string name = p.textures[i].name;
                if (!tc_texture_handle_is_invalid(p.textures[i].texture)) {
                    result[nb::cast(name)] = TcTexture(p.textures[i].texture);
                }
            }
            return result;
        })
        .def_prop_ro("uniforms", [](tc_material_phase& p) {
            nb::dict result;
            for (size_t i = 0; i < p.uniform_count; i++) {
                std::string name = p.uniforms[i].name;
                tc_uniform_value& u = p.uniforms[i];
                switch (u.type) {
                    case TC_UNIFORM_BOOL:
                    case TC_UNIFORM_INT:
                        result[nb::cast(name)] = u.data.i;
                        break;
                    case TC_UNIFORM_FLOAT:
                        result[nb::cast(name)] = u.data.f;
                        break;
                    case TC_UNIFORM_VEC2:
                        result[nb::cast(name)] = nb::make_tuple(u.data.v2[0], u.data.v2[1]);
                        break;
                    case TC_UNIFORM_VEC3:
                        result[nb::cast(name)] = Vec3{u.data.v3[0], u.data.v3[1], u.data.v3[2]};
                        break;
                    case TC_UNIFORM_VEC4:
                        result[nb::cast(name)] = Vec4{u.data.v4[0], u.data.v4[1], u.data.v4[2], u.data.v4[3]};
                        break;
                    default:
                        break;
                }
            }
            return result;
        })
        .def_prop_rw("state",
            [](tc_material_phase& p) { return p.state; },
            [](tc_material_phase& p, const tc_render_state& s) { p.state = s; })
        .def("set_uniform_float", [](tc_material_phase& p, const char* name, float v) {
            tc_material_phase_set_uniform(&p, name, TC_UNIFORM_FLOAT, &v);
        })
        .def("set_uniform_int", [](tc_material_phase& p, const char* name, int v) {
            tc_material_phase_set_uniform(&p, name, TC_UNIFORM_INT, &v);
        })
        .def("set_uniform_vec3", [](tc_material_phase& p, const char* name, const Vec3& v) {
            float arr[3] = {(float)v.x, (float)v.y, (float)v.z};
            tc_material_phase_set_uniform(&p, name, TC_UNIFORM_VEC3, arr);
        })
        .def("set_uniform_vec4", [](tc_material_phase& p, const char* name, const Vec4& v) {
            float arr[4] = {(float)v.x, (float)v.y, (float)v.z, (float)v.w};
            tc_material_phase_set_uniform(&p, name, TC_UNIFORM_VEC4, arr);
        })
        .def("set_texture", [](tc_material_phase& p, const char* name, TcTexture& tex) {
            tc_material_phase_set_texture(&p, name, tex.handle);
        })
        .def("set_color", [](tc_material_phase& p, float r, float g, float b, float a) {
            tc_material_phase_set_color(&p, r, g, b, a);
        })
        .def("set_available_marks", [](tc_material_phase& p, const std::vector<std::string>& marks) {
            p.available_mark_count = std::min(marks.size(), (size_t)TC_MATERIAL_MAX_MARKS);
            for (size_t i = 0; i < p.available_mark_count; i++) {
                strncpy(p.available_marks[i], marks[i].c_str(), TC_PHASE_MARK_MAX - 1);
                p.available_marks[i][TC_PHASE_MARK_MAX - 1] = '\0';
            }
        })
        .def("get_available_marks", [](tc_material_phase& p) -> std::vector<std::string> {
            std::vector<std::string> result;
            for (size_t i = 0; i < p.available_mark_count; i++) {
                result.push_back(p.available_marks[i]);
            }
            return result;
        })
        // available_marks property for Material API compatibility
        .def_prop_ro("available_marks", [](tc_material_phase& p) -> std::vector<std::string> {
            std::vector<std::string> result;
            for (size_t i = 0; i < p.available_mark_count; i++) {
                result.push_back(p.available_marks[i]);
            }
            return result;
        })
        // set_phase_mark for Material API compatibility
        .def("set_phase_mark", [](tc_material_phase& p, const std::string& mark) {
            strncpy(p.phase_mark, mark.c_str(), TC_PHASE_MARK_MAX - 1);
            p.phase_mark[TC_PHASE_MARK_MAX - 1] = '\0';
        }, nb::arg("mark"))
        // set_param for Material API compatibility (variant-based uniform setter)
        .def("set_param", [](tc_material_phase& p, const std::string& name, nb::object value) {
            if (nb::isinstance<nb::bool_>(value)) {
                int v = nb::cast<bool>(value) ? 1 : 0;
                tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_INT, &v);
            } else if (nb::isinstance<nb::int_>(value)) {
                int v = nb::cast<int>(value);
                tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_INT, &v);
            } else if (nb::isinstance<nb::float_>(value)) {
                float v = nb::cast<float>(value);
                tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_FLOAT, &v);
            } else if (nb::isinstance<Vec3>(value)) {
                Vec3 v = nb::cast<Vec3>(value);
                float arr[3] = {(float)v.x, (float)v.y, (float)v.z};
                tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_VEC3, arr);
            } else if (nb::isinstance<Vec4>(value)) {
                Vec4 v = nb::cast<Vec4>(value);
                float arr[4] = {(float)v.x, (float)v.y, (float)v.z, (float)v.w};
                tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_VEC4, arr);
            } else if (supports_python_buffer(value)) {
                auto arr = nb::cast<nb::ndarray<float, nb::c_contig, nb::device::cpu>>(value);
                size_t size = arr.size();
                float* ptr = arr.data();
                if (size == 2) {
                    tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_VEC2, ptr);
                } else if (size == 3) {
                    tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_VEC3, ptr);
                } else if (size == 4) {
                    tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_VEC4, ptr);
                } else if (size == 16) {
                    tc_material_phase_set_uniform(&p, name.c_str(), TC_UNIFORM_MAT4, ptr);
                } else {
                    tc::Log::error(
                        "tc_material_phase.set_param('%s') expects float32 buffer size 2, 3, 4, or 16; got %zu",
                        name.c_str(),
                        size);
                    throw std::runtime_error(
                        "tc_material_phase.set_param expects float32 buffer size 2, 3, 4, or 16");
                }
            }
        }, nb::arg("name"), nb::arg("value"));

    nb::class_<TcMaterial>(m, "TcMaterial")
        .def(nb::init<>())
        // kwargs constructor for Material API compatibility
        .def("__init__", [](TcMaterial* self, nb::kwargs kwargs) {
            // Create material - name is required
            if (!kwargs.contains("name")) {
                throw std::runtime_error("TcMaterial requires 'name' argument");
            }
            std::string mat_name = nb::cast<std::string>(kwargs["name"]);

            TcMaterial mat = TcMaterial::create(mat_name, "");
            if (!mat.is_valid()) {
                throw std::runtime_error("Failed to create TcMaterial");
            }

            // Get shader
            TcShader shader;
            if (kwargs.contains("shader")) {
                shader = nb::cast<TcShader>(kwargs["shader"]);
            } else if (kwargs.contains("shader_programm")) {
                shader = nb::cast<TcShader>(kwargs["shader_programm"]);
            }

            // Get render state
            tc_render_state rs = tc_render_state_opaque();
            if (kwargs.contains("render_state")) {
                nb::object rs_obj = nb::borrow<nb::object>(kwargs["render_state"]);
                if (nb::isinstance<tc_render_state>(rs_obj)) {
                    rs = nb::cast<tc_render_state>(rs_obj);
                } else if (nb::isinstance<RenderState>(rs_obj)) {
                    RenderState old_rs = nb::cast<RenderState>(rs_obj);
                    rs.depth_test = old_rs.depth_test;
                    rs.depth_write = old_rs.depth_write;
                    rs.blend = old_rs.blend;
                    rs.cull = old_rs.cull;
                }
            }

            std::string phase_mark = "opaque";
            if (kwargs.contains("phase_mark")) {
                phase_mark = nb::cast<std::string>(kwargs["phase_mark"]);
            }

            int priority = 0;
            if (kwargs.contains("priority")) {
                priority = nb::cast<int>(kwargs["priority"]);
            }

            // Add phase with shader
            tc_material_phase* phase = nullptr;
            if (shader.is_valid()) {
                phase = mat.add_phase(shader, phase_mark.c_str(), priority);
                if (phase) {
                    phase->state = rs;
                }
            }

            // Set other properties
            if (kwargs.contains("source_path")) {
                mat.set_source_path(nb::cast<std::string>(kwargs["source_path"]).c_str());
            }
            if (kwargs.contains("shader_name")) {
                mat.set_shader_name(nb::cast<std::string>(kwargs["shader_name"]).c_str());
            }

            // Set color
            if (kwargs.contains("color") && !kwargs["color"].is_none() && phase) {
                nb::object color_obj = nb::borrow<nb::object>(kwargs["color"]);
                if (nb::isinstance<Vec4>(color_obj)) {
                    Vec4 c = nb::cast<Vec4>(color_obj);
                    tc_material_phase_set_color(phase, c.x, c.y, c.z, c.w);
                } else if (supports_python_buffer(color_obj)) {
                    auto arr = nb::cast<nb::ndarray<float, nb::c_contig, nb::device::cpu>>(color_obj);
                    if (arr.size() != 4) {
                        tc::Log::error("TcMaterial(color) expects float32 buffer size 4; got %zu", arr.size());
                        throw std::runtime_error("TcMaterial(color) expects float32 buffer size 4");
                    }
                    float* ptr = arr.data();
                    tc_material_phase_set_color(phase, ptr[0], ptr[1], ptr[2], ptr[3]);
                } else if (nb::isinstance<nb::tuple>(color_obj) || nb::isinstance<nb::list>(color_obj)) {
                    nb::sequence seq = nb::cast<nb::sequence>(color_obj);
                    tc_material_phase_set_color(phase,
                        nb::cast<float>(seq[0]),
                        nb::cast<float>(seq[1]),
                        nb::cast<float>(seq[2]),
                        nb::cast<float>(seq[3])
                    );
                }
            }

            // Set textures
            if (kwargs.contains("textures") && !kwargs["textures"].is_none() && phase) {
                nb::dict tex_dict = nb::cast<nb::dict>(kwargs["textures"]);
                for (auto item : tex_dict) {
                    std::string key = nb::cast<std::string>(item.first);
                    nb::object val = nb::borrow<nb::object>(item.second);
                    if (nb::isinstance<TcTexture>(val)) {
                        TcTexture tex = nb::cast<TcTexture>(val);
                        tc_material_phase_set_texture(phase, key.c_str(), tex.handle);
                    } else {
                        TcTexture tex = require_tc_texture(val, "TcMaterial(textures)");
                        tc_material_phase_set_texture(phase, key.c_str(), tex.handle);
                    }
                }
            }

            // Set uniforms
            if (kwargs.contains("uniforms") && !kwargs["uniforms"].is_none() && phase) {
                nb::dict uniforms_dict = nb::cast<nb::dict>(kwargs["uniforms"]);
                for (auto item : uniforms_dict) {
                    std::string key = nb::cast<std::string>(item.first);
                    nb::object val = nb::borrow<nb::object>(item.second);
                    if (nb::isinstance<nb::bool_>(val)) {
                        int v = nb::cast<bool>(val) ? 1 : 0;
                        tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_INT, &v);
                    } else if (nb::isinstance<nb::int_>(val)) {
                        int v = nb::cast<int>(val);
                        tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_INT, &v);
                    } else if (nb::isinstance<nb::float_>(val)) {
                        float v = nb::cast<float>(val);
                        tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_FLOAT, &v);
                    } else if (nb::isinstance<Vec3>(val)) {
                        Vec3 v = nb::cast<Vec3>(val);
                        float arr[3] = {(float)v.x, (float)v.y, (float)v.z};
                        tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_VEC3, arr);
                    } else if (nb::isinstance<Vec4>(val)) {
                        Vec4 v = nb::cast<Vec4>(val);
                        float arr[4] = {(float)v.x, (float)v.y, (float)v.z, (float)v.w};
                        tc_material_phase_set_uniform(phase, key.c_str(), TC_UNIFORM_VEC4, arr);
                    }
                }
            }

            // Use placement new to construct in-place
            new (self) TcMaterial(std::move(mat));
        })
        .def_static("from_uuid", &TcMaterial::from_uuid, nb::arg("uuid"))
        .def_static("from_name", &TcMaterial::from_name, nb::arg("name"))
        .def_static("get_or_create", &TcMaterial::get_or_create, nb::arg("uuid"), nb::arg("name"))
        .def_static("create", &TcMaterial::create,
            nb::arg("name"), nb::arg("uuid_hint") = "")
        .def("copy", [](const TcMaterial& self, const std::string& new_uuid) {
            return TcMaterial::copy(self, new_uuid);
        }, nb::arg("new_uuid") = "")
        .def_prop_ro("is_valid", &TcMaterial::is_valid)
        .def_prop_ro("uuid", &TcMaterial::uuid)
        .def_prop_rw("name",
            &TcMaterial::name,
            &TcMaterial::set_name)
        .def_prop_ro("version", &TcMaterial::version)
        .def_prop_rw("shader_name",
            &TcMaterial::shader_name,
            &TcMaterial::set_shader_name)
        .def_prop_rw("source_path",
            &TcMaterial::source_path,
            &TcMaterial::set_source_path)
        .def_prop_ro("phase_count", &TcMaterial::phase_count)
        .def("get_phase", [](TcMaterial& self, size_t index) -> tc_material_phase* {
            return self.get_phase(index);
        }, nb::arg("index"), nb::rv_policy::reference)
        .def_prop_ro("phases", [](TcMaterial& self) {
            nb::list result;
            for (size_t i = 0; i < self.phase_count(); i++) {
                result.append(nb::cast(self.get_phase(i), nb::rv_policy::reference));
            }
            return result;
        })
        .def("default_phase", [](TcMaterial& self) -> tc_material_phase* {
            return self.default_phase();
        }, nb::rv_policy::reference)
        .def("clear_phases", &TcMaterial::clear_phases)
        .def("add_phase", [](
            TcMaterial& self,
            TcShader& shader,
            const std::string& phase_mark,
            int priority
        ) -> tc_material_phase* {
            if (!shader.is_valid()) {
                tc::Log::error(
                    "Failed to add phase '%s' to material '%s': shader is invalid",
                    phase_mark.c_str(),
                    self.name()
                );
                throw std::runtime_error("TcMaterial.add_phase requires a valid TcShader");
            }

            tc_material_phase* phase = self.add_phase(
                shader,
                phase_mark.c_str(),
                priority
            );
            if (!phase) {
                tc::Log::error(
                    "Failed to add phase '%s' to material '%s'",
                    phase_mark.c_str(),
                    self.name()
                );
                throw std::runtime_error("Failed to add material phase");
            }
            return phase;
        }, nb::arg("shader"), nb::arg("phase_mark") = "opaque",
           nb::arg("priority") = 0, nb::rv_policy::reference)
        .def("add_phase_from_sources", [](
            TcMaterial& self,
            const std::string& vertex_source,
            const std::string& fragment_source,
            const std::string& geometry_source,
            const std::string& shader_name,
            const std::string& phase_mark,
            int priority,
            const tc_render_state& state,
            const std::string& shader_uuid,
            int language,
            int artifact_policy,
            const std::string& vertex_entry,
            const std::string& fragment_entry,
            const std::string& geometry_entry
        ) -> tc_material_phase* {
            tc_shader_language shader_language = static_cast<tc_shader_language>(language);
            tc_shader_artifact_policy shader_artifact_policy =
                static_cast<tc_shader_artifact_policy>(artifact_policy);
            std::string vs = shader_language == TC_SHADER_LANGUAGE_GLSL
                ? rewrite_engine_uniforms_for_stage_source(vertex_source, "vertex")
                : vertex_source;
            std::string fs = shader_language == TC_SHADER_LANGUAGE_GLSL
                ? rewrite_engine_uniforms_for_stage_source(fragment_source, "fragment")
                : fragment_source;
            std::string gs;
            const char* gs_ptr = nullptr;
            if (!geometry_source.empty()) {
                gs = shader_language == TC_SHADER_LANGUAGE_GLSL
                    ? rewrite_engine_uniforms_for_stage_source(geometry_source, "geometry")
                    : geometry_source;
                gs_ptr = gs.c_str();
            }
            TcMaterialPhaseFromSourcesInfo phase_info;
            phase_info.shader.sources.vertex = vs;
            phase_info.shader.sources.fragment = fs;
            if (gs_ptr) {
                phase_info.shader.sources.geometry = gs;
            }
            phase_info.shader.sources.name = shader_name;
            phase_info.shader.sources.vertex_entry = vertex_entry;
            phase_info.shader.sources.fragment_entry = fragment_entry;
            phase_info.shader.sources.geometry_entry = geometry_entry;
            phase_info.shader.uuid = shader_uuid;
            phase_info.shader.language = shader_language;
            phase_info.shader.artifact_policy = shader_artifact_policy;
            phase_info.phase_mark = phase_mark;
            phase_info.priority = priority;
            phase_info.state = state;

            tc_material_phase* phase = self.add_phase_from_sources(phase_info);

            if (phase && shader_language == TC_SHADER_LANGUAGE_GLSL) {
                ShaderPhase raw_phase = infer_raw_glsl_resource_phase(vs, fs, gs);
                apply_parser_resource_layout(
                    tc_shader_get(phase->shader),
                    raw_phase,
                    MaterialUboLayout{},
                    shader_language);
            }
            return phase;
        }, nb::arg("vertex_source"), nb::arg("fragment_source"),
           nb::arg("geometry_source") = "", nb::arg("shader_name") = "",
           nb::arg("phase_mark") = "opaque", nb::arg("priority") = 0,
           nb::arg("state") = tc_render_state_opaque(),
           nb::arg("shader_uuid") = "",
           nb::arg("language") = static_cast<int>(TC_SHADER_LANGUAGE_GLSL),
           nb::arg("artifact_policy") = static_cast<int>(TC_SHADER_ARTIFACT_OPTIONAL),
           nb::arg("vertex_entry") = "",
           nb::arg("fragment_entry") = "",
           nb::arg("geometry_entry") = "",
           nb::rv_policy::reference)
        .def("bump_version", &TcMaterial::bump_version)
        // Color
        .def_prop_rw("color",
            [](const TcMaterial& self) -> nb::object {
                auto c = self.color();
                if (!c.has_value()) return nb::none();
                return nb::cast(c.value());
            },
            [](TcMaterial& self, nb::object val) {
                if (val.is_none()) return;
                if (nb::isinstance<Vec4>(val)) {
                    self.set_color(nb::cast<Vec4>(val));
                } else if (nb::isinstance<nb::tuple>(val) || nb::isinstance<nb::list>(val)) {
                    nb::sequence seq = nb::cast<nb::sequence>(val);
                    self.set_color(
                        nb::cast<float>(seq[0]),
                        nb::cast<float>(seq[1]),
                        nb::cast<float>(seq[2]),
                        nb::cast<float>(seq[3])
                    );
                }
            })
        .def("set_color", [](TcMaterial& self, const Vec4& c) {
            self.set_color(c.x, c.y, c.z, c.w);
        }, nb::arg("color"))
        .def("set_color", [](TcMaterial& self, float r, float g, float b, float a) {
            self.set_color(r, g, b, a);
        }, nb::arg("r"), nb::arg("g"), nb::arg("b"), nb::arg("a") = 1.0f)
        // Uniforms
        .def("set_uniform_float", &TcMaterial::set_uniform_float)
        .def("set_uniform_int", &TcMaterial::set_uniform_int)
        .def("set_uniform_vec3", &TcMaterial::set_uniform_vec3)
        .def("set_uniform_vec4", &TcMaterial::set_uniform_vec4)
        .def("set_uniform_mat4", [](TcMaterial& self, const char* name, const Mat44f& mat) {
            self.set_uniform_mat4(name, mat);
        })
        .def("set_uniform_mat4", [](TcMaterial& self, const char* name, const Mat44& mat) {
            self.set_uniform_mat4(name, mat.to_float());
        })
        .def("set_texture", [](TcMaterial& self, const char* name, TcTexture& tex) -> size_t {
            size_t applied = 0;
            for (size_t i = 0; i < self.phase_count(); i++) {
                tc_material_phase* phase = self.get_phase(i);
                if (!phase || !tc_material_phase_find_texture(phase, name)) {
                    continue;
                }
                if (tc_material_phase_set_texture(phase, name, tex.handle)) {
                    applied++;
                }
            }
            if (applied > 0) {
                self.bump_version();
            }
            return applied;
        }, nb::arg("name"), nb::arg("texture"),
           "Set a material texture on phases that already expose the texture slot.")
        // Phase access
        .def_prop_rw("active_phase_mark",
            &TcMaterial::active_phase_mark,
            &TcMaterial::set_active_phase_mark)
        // uniforms property for Material API compatibility (aggregated from all phases)
        .def_prop_ro("uniforms", [](TcMaterial& self) -> nb::dict {
            nb::dict result;
            for (size_t phase_index = 0; phase_index < self.phase_count(); phase_index++) {
                tc_material_phase* phase = self.get_phase(phase_index);
                if (!phase) continue;
                for (size_t i = 0; i < phase->uniform_count; i++) {
                    std::string name = phase->uniforms[i].name;
                    put_uniform_value(result, name, phase->uniforms[i]);
                }
            }
            return result;
        })
        // textures property for Material API compatibility (aggregated from all phases)
        .def_prop_ro("textures", [](TcMaterial& self) -> nb::dict {
            nb::dict result;
            for (size_t phase_index = 0; phase_index < self.phase_count(); phase_index++) {
                tc_material_phase* phase = self.get_phase(phase_index);
                if (!phase) continue;
                for (size_t i = 0; i < phase->texture_count; i++) {
                    std::string name = phase->textures[i].name;
                    if (!tc_texture_handle_is_invalid(phase->textures[i].texture)) {
                        result[nb::cast(name)] = TcTexture(phase->textures[i].texture);
                    }
                }
            }
            return result;
        })
        // shader property for Material API compatibility (from default phase)
        .def_prop_ro("shader", [](TcMaterial& self) -> TcShader {
            tc_material_phase* phase = self.default_phase();
            if (!phase) return TcShader();
            return TcShader(phase->shader);
        })
        // Serialization
        .def("serialize", [](const TcMaterial& self) {
            nb::dict d;
            if (!self.is_valid()) {
                d["type"] = "none";
                return d;
            }
            d["uuid"] = self.uuid();
            d["name"] = self.name();
            d["type"] = "uuid";
            return d;
        });

    m.def("create_material_from_parsed", &create_material_from_parsed_py,
        nb::arg("program"),
        nb::arg("color") = nb::none(),
        nb::arg("textures") = nb::none(),
        nb::arg("uniforms") = nb::none(),
        nb::arg("name") = nb::none(),
        nb::arg("source_path") = nb::none(),
        nb::arg("shader_uuid") = "",
        nb::arg("default_white_texture") = nb::none(),
        nb::arg("default_normal_texture") = nb::none(),
        "Create a TcMaterial from a parsed ShaderMultyPhaseProgramm");

    // Material registry info functions
    m.def("tc_material_get_all_info", []() -> nb::list {
        nb::list result;
        size_t count = 0;
        tc_material_info* infos = tc_material_get_all_info(&count);
        if (!infos) return result;

        for (size_t i = 0; i < count; i++) {
            nb::dict d;
            d["uuid"] = infos[i].uuid;
            d["name"] = infos[i].name ? infos[i].name : "";
            d["ref_count"] = infos[i].ref_count;
            d["version"] = infos[i].version;
            d["phase_count"] = infos[i].phase_count;
            d["texture_count"] = infos[i].texture_count;
            result.append(d);
        }

        free(infos);
        return result;
    }, "Get info for all materials in the registry");

    m.def("tc_material_count", []() -> size_t {
        return tc_material_count();
    }, "Get number of materials in the registry");
}

} // namespace termin
