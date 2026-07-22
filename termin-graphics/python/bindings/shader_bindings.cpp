// shader_bindings.cpp - TcShader Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/optional.h>
#include <nanobind/ndarray.h>

#include <cstring>
#include <vector>

#include "tgfx/tgfx_shader_handle.hpp"
#include "tgfx/tgfx_shader_program_handle.hpp"
#include "tgfx/resources/tc_shader.h"
#include "tgfx2/builtin_shader_sources.hpp"

namespace nb = nanobind;

using namespace termin;

namespace tgfx_bindings {

namespace {

const char* shader_resource_kind_name(uint32_t kind) {
    switch (kind) {
        case TC_SHADER_RESOURCE_NONE: return "none";
        case TC_SHADER_RESOURCE_CONSTANT_BUFFER: return "constant_buffer";
        case TC_SHADER_RESOURCE_TEXTURE: return "texture";
        case TC_SHADER_RESOURCE_SAMPLER: return "sampler";
        case TC_SHADER_RESOURCE_STORAGE_BUFFER: return "storage_buffer";
        case TC_SHADER_RESOURCE_STORAGE_TEXTURE: return "storage_texture";
        default: return "unknown";
    }
}

const char* shader_resource_scope_name(uint32_t scope) {
    switch (scope) {
        case TC_SHADER_RESOURCE_SCOPE_UNKNOWN: return "unknown";
        case TC_SHADER_RESOURCE_SCOPE_FRAME: return "frame";
        case TC_SHADER_RESOURCE_SCOPE_PASS: return "pass";
        case TC_SHADER_RESOURCE_SCOPE_MATERIAL: return "material";
        case TC_SHADER_RESOURCE_SCOPE_DRAW: return "draw";
        case TC_SHADER_RESOURCE_SCOPE_TRANSIENT: return "transient";
        case TC_SHADER_RESOURCE_SCOPE_UNSCOPED: return "unscoped";
        default: return "unknown";
    }
}

nb::dict shader_resource_binding_to_dict(const tc_shader_resource_binding& binding) {
    nb::dict result;
    result["name"] = std::string(binding.name);
    result["kind"] = binding.kind;
    result["kind_name"] = shader_resource_kind_name(binding.kind);
    result["scope"] = binding.scope;
    result["scope_name"] = shader_resource_scope_name(binding.scope);
    result["set"] = binding.set;
    result["binding"] = binding.binding;
    result["stage_mask"] = binding.stage_mask;
    result["size"] = binding.size;
    if (binding.fields && binding.field_count > 0) {
        nb::list fields;
        for (uint32_t i = 0; i < binding.field_count; ++i) {
            nb::dict fd;
            fd["name"] = std::string(binding.fields[i].name);
            fd["type"] = std::string(binding.fields[i].type);
            fd["offset"] = binding.fields[i].offset;
            fd["size"] = binding.fields[i].size;
            fields.append(fd);
        }
        result["fields"] = fields;
    }
    return result;
}

nb::dict shader_resource_requirement_to_dict(
    const tc_shader_resource_requirement& requirement)
{
    nb::dict result;
    result["name"] = std::string(requirement.name);
    result["kind"] = requirement.kind;
    result["kind_name"] = shader_resource_kind_name(requirement.kind);
    result["scope"] = requirement.scope;
    result["scope_name"] = shader_resource_scope_name(requirement.scope);
    result["stage_mask"] = requirement.stage_mask;
    result["size"] = requirement.size;
    result["element_stride"] = requirement.element_stride;
    if (requirement.fields && requirement.field_count > 0) {
        nb::list fields;
        for (uint32_t i = 0; i < requirement.field_count; ++i) {
            nb::dict fd;
            fd["name"] = std::string(requirement.fields[i].name);
            fd["type"] = std::string(requirement.fields[i].type);
            fd["offset"] = requirement.fields[i].offset;
            fd["size"] = requirement.fields[i].size;
            fields.append(fd);
        }
        result["fields"] = fields;
    }
    return result;
}

nb::dict shader_program_render_state_to_dict(const tc_render_state& state) {
    nb::dict result;
    result["polygon_mode"] = state.polygon_mode;
    result["cull"] = state.cull != 0;
    result["depth_test"] = state.depth_test != 0;
    result["depth_write"] = state.depth_write != 0;
    result["blend"] = state.blend != 0;
    result["blend_src"] = state.blend_src;
    result["blend_dst"] = state.blend_dst;
    result["depth_func"] = state.depth_func;
    return result;
}

tc_render_state shader_program_render_state_from_dict(const nb::dict& values) {
    tc_render_state result = tc_render_state_opaque();
    if (values.contains("polygon_mode")) {
        result.polygon_mode = nb::cast<uint8_t>(values["polygon_mode"]);
    }
    if (values.contains("cull")) result.cull = nb::cast<bool>(values["cull"]);
    if (values.contains("depth_test")) result.depth_test = nb::cast<bool>(values["depth_test"]);
    if (values.contains("depth_write")) result.depth_write = nb::cast<bool>(values["depth_write"]);
    if (values.contains("blend")) result.blend = nb::cast<bool>(values["blend"]);
    if (values.contains("blend_src")) {
        result.blend_src = nb::cast<uint8_t>(values["blend_src"]);
    }
    if (values.contains("blend_dst")) {
        result.blend_dst = nb::cast<uint8_t>(values["blend_dst"]);
    }
    if (values.contains("depth_func")) {
        result.depth_func = nb::cast<uint8_t>(values["depth_func"]);
    }
    return result;
}

[[noreturn]] void throw_property_default_error(
    const std::string& name,
    const std::string& property_type,
    const std::string& detail)
{
    throw std::invalid_argument(
        "TcShaderProgram property '" + name + "' (" + property_type + ") " + detail);
}

void set_shader_program_property_default(
    const std::string& name,
    const std::string& property_type,
    nb::handle object,
    tc_uniform_value& default_value,
    std::string& default_text)
{
    if (property_type == "Bool") {
        if (!nb::isinstance<nb::bool_>(object)) {
            throw_property_default_error(name, property_type, "default must be bool");
        }
        default_value.type = TC_UNIFORM_BOOL;
        default_value.data.i = nb::cast<bool>(object) ? 1 : 0;
        return;
    }
    if (property_type == "Int") {
        if (nb::isinstance<nb::bool_>(object) || !nb::isinstance<nb::int_>(object)) {
            throw_property_default_error(name, property_type, "default must be int");
        }
        default_value.type = TC_UNIFORM_INT;
        default_value.data.i = nb::cast<int32_t>(object);
        return;
    }
    if (property_type == "Float") {
        const bool numeric = !nb::isinstance<nb::bool_>(object)
            && (nb::isinstance<nb::int_>(object) || nb::isinstance<nb::float_>(object));
        if (!numeric) {
            throw_property_default_error(name, property_type, "default must be numeric");
        }
        default_value.type = TC_UNIFORM_FLOAT;
        default_value.data.f = nb::cast<float>(object);
        return;
    }
    if (property_type == "Texture" || property_type == "Texture2D") {
        if (!nb::isinstance<nb::str>(object)) {
            throw_property_default_error(name, property_type, "default must be a texture name");
        }
        default_text = nb::cast<std::string>(object);
        return;
    }

    size_t expected_size = 0;
    tc_uniform_type uniform_type = TC_UNIFORM_NONE;
    if (property_type == "Vec2") {
        expected_size = 2;
        uniform_type = TC_UNIFORM_VEC2;
    } else if (property_type == "Vec3") {
        expected_size = 3;
        uniform_type = TC_UNIFORM_VEC3;
    } else if (property_type == "Vec4" || property_type == "Color") {
        expected_size = 4;
        uniform_type = TC_UNIFORM_VEC4;
    } else if (property_type == "Mat4") {
        expected_size = 16;
        uniform_type = TC_UNIFORM_MAT4;
    } else {
        throw_property_default_error(name, property_type, "has an unsupported default type");
    }

    if (!nb::isinstance<nb::tuple>(object) && !nb::isinstance<nb::list>(object)) {
        throw_property_default_error(name, property_type, "default must be a sequence");
    }
    nb::sequence sequence = nb::cast<nb::sequence>(object);
    const size_t size = nb::len(sequence);
    if (size != expected_size) {
        throw_property_default_error(
            name,
            property_type,
            "default requires exactly " + std::to_string(expected_size) + " components; got "
                + std::to_string(size));
    }

    default_value.type = uniform_type;
    float* destination = nullptr;
    switch (uniform_type) {
        case TC_UNIFORM_VEC2: destination = default_value.data.v2; break;
        case TC_UNIFORM_VEC3: destination = default_value.data.v3; break;
        case TC_UNIFORM_VEC4: destination = default_value.data.v4; break;
        case TC_UNIFORM_MAT4: destination = default_value.data.m4; break;
        default: break;
    }
    for (size_t component = 0; component < expected_size; ++component) {
        destination[component] = nb::cast<float>(sequence[component]);
    }
}

} // namespace

void bind_shader(nb::module_& m) {
    // tc_shader_handle - C struct
    nb::class_<tc_shader_handle>(m, "TcShaderHandle")
        .def(nb::init<>())
        .def_rw("index", &tc_shader_handle::index)
        .def_rw("generation", &tc_shader_handle::generation)
        .def("is_invalid", [](const tc_shader_handle& h) {
            return tc_shader_handle_is_invalid(h);
        })
        .def("__repr__", [](const tc_shader_handle& h) {
            return "<TcShaderHandle index=" + std::to_string(h.index) +
                   " gen=" + std::to_string(h.generation) + ">";
        });

    // Shader variant operation enum
    nb::enum_<tc_shader_variant_op>(m, "ShaderVariantOp")
        .value("NONE", TC_SHADER_VARIANT_NONE)
        .value("SKINNING", TC_SHADER_VARIANT_SKINNING)
        .value("INSTANCING", TC_SHADER_VARIANT_INSTANCING)
        .value("MORPHING", TC_SHADER_VARIANT_MORPHING)
        .value("FOLIAGE", TC_SHADER_VARIANT_FOLIAGE)
        .value("FOLIAGE_SHADOW", TC_SHADER_VARIANT_FOLIAGE_SHADOW)
        .value("LINE_MATERIAL_FRAGMENT", TC_SHADER_VARIANT_LINE_MATERIAL_FRAGMENT)
        .value("LINE_TUBE_BODY", TC_SHADER_VARIANT_LINE_TUBE_BODY)
        .value("LINE_TUBE_CAP", TC_SHADER_VARIANT_LINE_TUBE_CAP);

    // Shader feature flags enum
    nb::enum_<tc_shader_feature>(m, "ShaderFeature")
        .value("NONE", TC_SHADER_FEATURE_NONE)
        .value("LIGHTING_UBO", TC_SHADER_FEATURE_LIGHTING_UBO);

    nb::enum_<tc_shader_language>(m, "ShaderLanguage")
        .value("GLSL", TC_SHADER_LANGUAGE_GLSL)
        .value("SLANG", TC_SHADER_LANGUAGE_SLANG)
        .value("HLSL", TC_SHADER_LANGUAGE_HLSL);

    nb::enum_<tc_shader_artifact_policy>(m, "ShaderArtifactPolicy")
        .value("OPTIONAL", TC_SHADER_ARTIFACT_OPTIONAL)
        .value("REQUIRED", TC_SHADER_ARTIFACT_REQUIRED);

    nb::enum_<tc_shader_resource_kind>(m, "ShaderResourceKind")
        .value("NONE", TC_SHADER_RESOURCE_NONE)
        .value("CONSTANT_BUFFER", TC_SHADER_RESOURCE_CONSTANT_BUFFER)
        .value("TEXTURE", TC_SHADER_RESOURCE_TEXTURE)
        .value("SAMPLER", TC_SHADER_RESOURCE_SAMPLER)
        .value("STORAGE_BUFFER", TC_SHADER_RESOURCE_STORAGE_BUFFER)
        .value("STORAGE_TEXTURE", TC_SHADER_RESOURCE_STORAGE_TEXTURE);

    nb::enum_<tc_shader_resource_scope>(m, "ShaderResourceScope")
        .value("UNKNOWN", TC_SHADER_RESOURCE_SCOPE_UNKNOWN)
        .value("FRAME", TC_SHADER_RESOURCE_SCOPE_FRAME)
        .value("PASS", TC_SHADER_RESOURCE_SCOPE_PASS)
        .value("MATERIAL", TC_SHADER_RESOURCE_SCOPE_MATERIAL)
        .value("DRAW", TC_SHADER_RESOURCE_SCOPE_DRAW)
        .value("TRANSIENT", TC_SHADER_RESOURCE_SCOPE_TRANSIENT)
        .value("UNSCOPED", TC_SHADER_RESOURCE_SCOPE_UNSCOPED);

    // TcShader - RAII wrapper
    nb::class_<TcShader>(m, "TcShader")
        .def(nb::init<>())
        .def(nb::init<tc_shader_handle>(), nb::arg("handle"))
        .def_prop_ro("handle", [](const TcShader& s) { return s.handle; })
        .def_prop_ro("is_valid", &TcShader::is_valid)
        .def_prop_ro("uuid", [](const TcShader& s) { return std::string(s.uuid()); })
        .def_prop_ro("name", [](const TcShader& s) { return std::string(s.name()); })
        .def_prop_ro("source_path", [](const TcShader& s) { return std::string(s.source_path()); })
        .def_prop_ro("version", &TcShader::version)
        .def_prop_ro("source_hash", [](const TcShader& s) { return std::string(s.source_hash()); })
        .def_prop_ro("vertex_source", [](const TcShader& s) { return std::string(s.vertex_source()); })
        .def_prop_ro("fragment_source", [](const TcShader& s) { return std::string(s.fragment_source()); })
        .def_prop_ro("geometry_source", [](const TcShader& s) { return std::string(s.geometry_source()); })
        .def_prop_ro("vertex_entry", [](const TcShader& s) {
            const tc_shader* shader = s.get();
            return std::string(shader && shader->vertex_entry ? shader->vertex_entry : "");
        })
        .def_prop_ro("fragment_entry", [](const TcShader& s) {
            const tc_shader* shader = s.get();
            return std::string(shader && shader->fragment_entry ? shader->fragment_entry : "");
        })
        .def_prop_ro("geometry_entry", [](const TcShader& s) {
            const tc_shader* shader = s.get();
            return std::string(shader && shader->geometry_entry ? shader->geometry_entry : "");
        })
        .def_prop_ro("has_geometry", &TcShader::has_geometry)
        .def_prop_ro("is_variant", &TcShader::is_variant)
        .def_prop_ro("variant_op", &TcShader::variant_op)
        .def_prop_ro("features", &TcShader::features)
        .def_prop_ro("language", &TcShader::language)
        .def_prop_ro("artifact_policy", &TcShader::artifact_policy)
        .def_prop_ro("requires_artifacts", &TcShader::requires_artifacts)
        .def_prop_ro("has_contract", [](const TcShader& s) {
            return tc_shader_has_contract(s.get());
        })
        .def_prop_ro("contract", [](const TcShader& s) -> nb::object {
            tc_shader_contract_view view{};
            if (!tc_shader_get_contract_view(s.get(), &view)) {
                return nb::none();
            }

            nb::dict result;
            result["schema_version"] = view.schema_version;
            result["source_kind"] = view.source_kind;
            result["debug_name"] =
                view.debug_name ? std::string(view.debug_name) : std::string();
            result["source_debug_name"] =
                view.source_debug_name
                    ? std::string(view.source_debug_name)
                    : std::string();

            nb::list vertex_inputs;
            for (uint32_t i = 0; i < view.vertex_input_count; ++i) {
                nb::dict input;
                input["semantic"] = std::string(view.vertex_inputs[i].semantic);
                input["type"] = view.vertex_inputs[i].type;
                input["required"] = view.vertex_inputs[i].required != 0;
                vertex_inputs.append(input);
            }
            result["vertex_inputs"] = vertex_inputs;

            nb::list resources;
            for (uint32_t i = 0; i < view.resource_count; ++i) {
                resources.append(shader_resource_requirement_to_dict(view.resources[i]));
            }
            result["resources"] = resources;
            return result;
        })
        .def_prop_ro("_raw_ptr", [](const TcShader& s) -> const ::tc_shader* {
            return s.get();
        })
        .def_prop_ro("resource_binding_count", &TcShader::resource_binding_count)
        .def("find_resource_binding",
            [](const TcShader& self, const std::string& name) -> nb::object {
                const tc_shader_resource_binding* binding =
                    self.find_resource_binding(name.c_str());
                if (!binding) return nb::none();
                return shader_resource_binding_to_dict(*binding);
            },
            nb::arg("name"),
            "Return resource layout entry by shader resource name, or None")
        .def("set_resource_layout",
            [](TcShader& self,
               const std::vector<std::tuple<std::string, tc_shader_resource_kind, tc_shader_resource_scope, uint32_t, uint32_t, uint32_t, uint32_t>>& entries
            ) {
                tc_shader* shader = self.get();
                if (!shader) return;
                if (entries.empty()) {
                    tc_shader_set_resource_layout(shader, nullptr, 0);
                    return;
                }
                std::vector<tc_shader_resource_binding> bindings;
                bindings.reserve(entries.size());
                for (const auto& item : entries) {
                    tc_shader_resource_binding binding{};
                    const std::string& name = std::get<0>(item);
                    std::strncpy(binding.name, name.c_str(), TC_SHADER_RESOURCE_NAME_MAX - 1);
                    binding.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
                    binding.kind = static_cast<uint32_t>(std::get<1>(item));
                    binding.scope = static_cast<uint32_t>(std::get<2>(item));
                    binding.set = std::get<3>(item);
                    binding.binding = std::get<4>(item);
                    binding.stage_mask = std::get<5>(item);
                    binding.size = std::get<6>(item);
                    bindings.push_back(binding);
                }
                tc_shader_set_resource_layout(
                    shader,
                    bindings.data(),
                    static_cast<uint32_t>(bindings.size()));
            },
            nb::arg("entries"),
            "Replace shader resource layout with tuples: "
            "(name, kind, scope, set, binding, stage_mask, size)")
        .def("has_feature", &TcShader::has_feature, nb::arg("feature"))
        .def("set_feature", &TcShader::set_feature, nb::arg("feature"))
        .def("set_features", &TcShader::set_features, nb::arg("features"))
        .def("set_language", &TcShader::set_language, nb::arg("language"))
        .def("set_artifact_policy", &TcShader::set_artifact_policy, nb::arg("policy"))
        .def("variant_is_stale", &TcShader::variant_is_stale)
        .def("original", &TcShader::original)
        .def("set_variant_info", &TcShader::set_variant_info)
        .def_static("from_sources",
            [](const std::string& vertex,
               const std::string& fragment,
               const std::string& geometry,
               const std::string& name,
               const std::string& source_path,
               std::optional<tc_shader_language> language,
               tc_shader_artifact_policy artifact_policy,
               const std::string& vertex_entry,
               const std::string& fragment_entry,
               const std::string& geometry_entry) {
                if (!language.has_value()) {
                    throw std::invalid_argument(
                        "TcShader.from_sources requires an explicit shader language");
                }
                TcShaderCreateInfo create_info{};
                create_info.sources.vertex = vertex;
                create_info.sources.fragment = fragment;
                create_info.sources.geometry = geometry;
                create_info.sources.name = name;
                create_info.sources.source_path = source_path;
                create_info.sources.vertex_entry = vertex_entry;
                create_info.sources.fragment_entry = fragment_entry;
                create_info.sources.geometry_entry = geometry_entry;
                create_info.language = *language;
                create_info.artifact_policy = artifact_policy;
                return TcShader::from_sources(create_info);
            },
            nb::arg("vertex"), nb::arg("fragment"),
            nb::arg("geometry") = "", nb::arg("name") = "", nb::arg("source_path") = "",
            nb::arg("language") = nb::none(),
            nb::arg("artifact_policy") = TC_SHADER_ARTIFACT_OPTIONAL,
            nb::arg("vertex_entry") = "",
            nb::arg("fragment_entry") = "",
            nb::arg("geometry_entry") = "")
        .def_static("from_builtin_catalog",
            [](const std::string& uuid) {
                const tc_shader_handle h =
                    tgfx::register_builtin_shader_from_catalog(uuid.c_str());
                if (tc_shader_handle_is_invalid(h)) {
                    return TcShader();
                }
                return TcShader(h);
            },
            nb::arg("uuid"),
            "Register and return a built-in engine shader by catalog UUID")
        .def_static("from_uuid", &TcShader::from_uuid)
        .def_static("from_hash", &TcShader::from_hash)
        .def_static("from_name", &TcShader::from_name)
        .def_static("get_or_create", &TcShader::get_or_create, nb::arg("uuid"),
            "Get existing tc_shader by UUID or create new one")
        .def("set_sources",
            [](TcShader& self,
               const std::string& vertex,
               const std::string& fragment,
               const std::string& geometry,
               const std::string& name,
               const std::string& source_path) {
                return self.set_sources(vertex, fragment, geometry, name, source_path);
            },
            nb::arg("vertex"), nb::arg("fragment"),
            nb::arg("geometry") = "", nb::arg("name") = "", nb::arg("source_path") = "",
            "Set shader sources (bumps version if changed)")
        .def("set_sources_with_entries",
            [](TcShader& self,
               const std::string& vertex,
               const std::string& fragment,
               const std::string& geometry,
               const std::string& name,
               const std::string& source_path,
               const std::string& vertex_entry,
               const std::string& fragment_entry,
               const std::string& geometry_entry) {
                TcShaderSources sources{};
                sources.vertex = vertex;
                sources.fragment = fragment;
                sources.geometry = geometry;
                sources.name = name;
                sources.source_path = source_path;
                sources.vertex_entry = vertex_entry;
                sources.fragment_entry = fragment_entry;
                sources.geometry_entry = geometry_entry;
                return self.set_sources(sources);
            },
            nb::arg("vertex"), nb::arg("fragment"),
            nb::arg("geometry") = "", nb::arg("name") = "", nb::arg("source_path") = "",
            nb::arg("vertex_entry") = "",
            nb::arg("fragment_entry") = "",
            nb::arg("geometry_entry") = "",
            "Set shader sources and explicit stage entry points")
        // Push a std140 material UBO layout onto the shader. `entries`
        // is a list of (name, property_type, offset, size) tuples
        // produced by the parser (see ShaderPhase.material_ubo_layout).
        // Called from ShaderAsset._update_tc_shaders after set_sources
        // so the layout always matches the current source version.
        .def("set_material_ubo_layout",
            [](TcShader& self,
               const std::vector<std::tuple<std::string, std::string, uint32_t, uint32_t>>& entries,
               uint32_t block_size
            ) {
                tc_shader* s = self.get();
                if (!s) return;
                if (entries.empty()) {
                    tc_shader_set_material_ubo_layout(s, nullptr, 0, 0);
                    return;
                }
                std::vector<tc_material_ubo_entry> buf;
                buf.reserve(entries.size());
                for (const auto& t : entries) {
                    tc_material_ubo_entry e{};
                    const std::string& name = std::get<0>(t);
                    const std::string& type = std::get<1>(t);
                    std::strncpy(e.name, name.c_str(), TC_MATERIAL_UBO_NAME_MAX - 1);
                    e.name[TC_MATERIAL_UBO_NAME_MAX - 1] = '\0';
                    std::strncpy(e.property_type, type.c_str(), TC_MATERIAL_UBO_TYPE_MAX - 1);
                    e.property_type[TC_MATERIAL_UBO_TYPE_MAX - 1] = '\0';
                    e.offset = std::get<2>(t);
                    e.size   = std::get<3>(t);
                    buf.push_back(e);
                }
                tc_shader_set_material_ubo_layout(
                    s, buf.data(), static_cast<uint32_t>(buf.size()), block_size);
            },
            nb::arg("entries"), nb::arg("block_size"),
            "Push std140 material UBO layout from parser onto this shader")
        .def_prop_ro("material_ubo_block_size",
            [](const TcShader& self) {
                tc_shader* s = self.get();
                return s ? s->material_ubo_block_size : 0u;
            })
        .def_prop_ro("material_ubo_entry_count",
            [](const TcShader& self) {
                tc_shader* s = self.get();
                return s ? s->material_ubo_entry_count : 0u;
            })
        .def("__repr__", [](const TcShader& s) {
            if (!s.is_valid()) return std::string("<TcShader invalid>");
            std::string name = s.name();
            if (name.empty()) name = s.uuid();
            return "<TcShader " + name + " v" + std::to_string(s.version()) + ">";
        });

    nb::class_<TcShaderProgram>(m, "TcShaderProgram")
        .def(nb::init<>())
        .def_static("declare", &TcShaderProgram::declare, nb::arg("uuid"), nb::arg("name"))
        .def_static("find", &TcShaderProgram::find, nb::arg("uuid"))
        .def_prop_ro("is_valid", &TcShaderProgram::is_valid)
        .def_prop_ro("uuid", [](const TcShaderProgram& value) {
            return std::string(value.uuid());
        })
        .def_prop_ro("name", [](const TcShaderProgram& value) {
            return std::string(value.name());
        })
        .def_prop_ro("version", &TcShaderProgram::version)
        .def_prop_ro("source_path", [](const TcShaderProgram& value) {
            const tc_shader_program* program = value.get();
            return std::string(program && program->source_path ? program->source_path : "");
        })
        .def_prop_ro("language", [](const TcShaderProgram& value) {
            const tc_shader_program* program = value.get();
            return std::string(program && program->language ? program->language : "");
        })
        .def_prop_ro("features", [](const TcShaderProgram& value) {
            const tc_shader_program* program = value.get();
            return program ? program->features : 0u;
        })
        .def_prop_ro("properties", [](const TcShaderProgram& value) {
            nb::list result;
            const tc_shader_program* program = value.get();
            if (!program) return result;
            for (uint32_t i = 0; i < program->property_count; ++i) {
                const tc_shader_program_property& property = program->properties[i];
                nb::dict item;
                item["name"] = std::string(property.name);
                item["property_type"] = std::string(property.property_type);
                item["label"] = std::string(property.label);
                item["has_default"] = property.has_default != 0;
                if (property.has_default) {
                    if (property.default_text[0] != '\0') {
                        item["default"] = std::string(property.default_text);
                    } else {
                    switch ((tc_uniform_type)property.default_value.type) {
                        case TC_UNIFORM_BOOL:
                            item["default"] = property.default_value.data.i != 0;
                            break;
                        case TC_UNIFORM_INT:
                            item["default"] = property.default_value.data.i;
                            break;
                        case TC_UNIFORM_FLOAT:
                            item["default"] = property.default_value.data.f;
                            break;
                        case TC_UNIFORM_VEC2:
                            item["default"] = nb::make_tuple(
                                property.default_value.data.v2[0],
                                property.default_value.data.v2[1]);
                            break;
                        case TC_UNIFORM_VEC3:
                            item["default"] = nb::make_tuple(
                                property.default_value.data.v3[0],
                                property.default_value.data.v3[1],
                                property.default_value.data.v3[2]);
                            break;
                        case TC_UNIFORM_VEC4:
                            item["default"] = nb::make_tuple(
                                property.default_value.data.v4[0],
                                property.default_value.data.v4[1],
                                property.default_value.data.v4[2],
                                property.default_value.data.v4[3]);
                            break;
                        case TC_UNIFORM_MAT4: {
                            nb::list components;
                            for (size_t component = 0; component < 16; ++component) {
                                components.append(property.default_value.data.m4[component]);
                            }
                            item["default"] = nb::tuple(components);
                            break;
                        }
                        default:
                            item["default"] = nb::none();
                            break;
                    }
                    }
                }
                item["range_min"] = property.has_range_min
                    ? nb::cast(property.range_min)
                    : nb::none();
                item["range_max"] = property.has_range_max
                    ? nb::cast(property.range_max)
                    : nb::none();
                result.append(item);
            }
            return result;
        })
        .def_prop_ro("phases", [](const TcShaderProgram& value) {
            nb::list result;
            const tc_shader_program* program = value.get();
            if (!program) return result;
            for (uint32_t i = 0; i < program->phase_count; ++i) {
                const tc_shader_program_phase& phase = program->phases[i];
                nb::dict item;
                item["phase_mark"] = std::string(phase.phase_mark);
                item["priority"] = phase.priority;
                item["state"] = shader_program_render_state_to_dict(phase.state);
                item["shader"] = TcShader(phase.shader);
                result.append(item);
            }
            return result;
        })
        .def("set_payload", [](
            TcShaderProgram& value,
            const std::string& name,
            const std::string& source_path,
            const std::string& language,
            uint32_t features,
            nb::list property_values,
            nb::list phase_values
        ) {
            tc_shader_program* program = value.get();
            if (!program) throw std::runtime_error("TcShaderProgram is stale");

            std::vector<std::string> property_names;
            std::vector<std::string> property_types;
            std::vector<std::string> property_labels;
            std::vector<tc_uniform_value> defaults;
            std::vector<uint8_t> has_defaults;
            std::vector<std::string> default_texts;
            std::vector<tc_shader_program_property_desc> properties;
            const size_t property_count = nb::len(property_values);
            property_names.reserve(property_count);
            property_types.reserve(property_count);
            property_labels.reserve(property_count);
            defaults.resize(property_count);
            has_defaults.resize(property_count);
            default_texts.resize(property_count);
            properties.reserve(property_count);
            for (size_t i = 0; i < property_count; ++i) {
                nb::dict item = nb::cast<nb::dict>(property_values[i]);
                property_names.push_back(nb::cast<std::string>(item["name"]));
                property_types.push_back(nb::cast<std::string>(item["property_type"]));
                property_labels.push_back(
                    item.contains("label") ? nb::cast<std::string>(item["label"]) : std::string());
                tc_uniform_value& default_value = defaults[i];
                memset(&default_value, 0, sizeof(default_value));
                if (item.contains("default") && !item["default"].is_none()) {
                    nb::object object = nb::borrow<nb::object>(item["default"]);
                    has_defaults[i] = 1;
                    set_shader_program_property_default(
                        property_names.back(),
                        property_types.back(),
                        object,
                        default_value,
                        default_texts[i]);
                }
                tc_shader_program_property_desc desc{};
                desc.name = property_names.back().c_str();
                desc.property_type = property_types.back().c_str();
                desc.label = property_labels.back().empty() ? nullptr : property_labels.back().c_str();
                desc.default_value = has_defaults[i] && default_texts[i].empty()
                    ? &defaults[i]
                    : nullptr;
                desc.default_text = default_texts[i].empty() ? nullptr : default_texts[i].c_str();
                if (item.contains("range_min") && !item["range_min"].is_none()) {
                    desc.range_min = nb::cast<double>(item["range_min"]);
                    desc.has_range_min = 1;
                }
                if (item.contains("range_max") && !item["range_max"].is_none()) {
                    desc.range_max = nb::cast<double>(item["range_max"]);
                    desc.has_range_max = 1;
                }
                properties.push_back(desc);
            }

            std::vector<std::string> phase_marks;
            std::vector<tc_shader_program_phase_desc> phases;
            const size_t phase_count = nb::len(phase_values);
            phase_marks.reserve(phase_count);
            phases.reserve(phase_count);
            for (size_t i = 0; i < phase_count; ++i) {
                nb::dict item = nb::cast<nb::dict>(phase_values[i]);
                phase_marks.push_back(nb::cast<std::string>(item["phase_mark"]));
                tc_shader_program_phase_desc desc{};
                desc.phase_mark = phase_marks.back().c_str();
                desc.priority = item.contains("priority") ? nb::cast<int32_t>(item["priority"]) : 0;
                desc.state = item.contains("state")
                    ? shader_program_render_state_from_dict(nb::cast<nb::dict>(item["state"]))
                    : tc_render_state_opaque();
                phases.push_back(desc);
            }

            tc_shader_program_payload_desc desc{};
            desc.name = name.c_str();
            desc.source_path = source_path.empty() ? nullptr : source_path.c_str();
            desc.language = language.empty() ? nullptr : language.c_str();
            desc.features = features;
            desc.properties = properties.empty() ? nullptr : properties.data();
            desc.property_count = static_cast<uint32_t>(properties.size());
            desc.phases = phases.empty() ? nullptr : phases.data();
            desc.phase_count = static_cast<uint32_t>(phases.size());
            if (!tc_shader_program_set_payload(program, &desc)) {
                throw std::runtime_error("TcShaderProgram payload was rejected");
            }
        },
        nb::arg("name"),
        nb::arg("source_path") = "",
        nb::arg("language") = "",
        nb::arg("features") = 0,
        nb::arg("properties") = nb::list(),
        nb::arg("phases") = nb::list())
        .def_static("make_phase_uuid", [](
            const std::string& program_uuid,
            const std::string& phase_mark
        ) {
            char result[TC_UUID_SIZE];
            tc_shader_program_make_phase_uuid(
                result, sizeof(result), program_uuid.c_str(), phase_mark.c_str());
            return std::string(result);
        }, nb::arg("program_uuid"), nb::arg("phase_mark"))
        .def("find_phase", [](const TcShaderProgram& value, const std::string& mark) -> nb::object {
            const tc_shader_program_phase* phase = tc_shader_program_find_phase(value.get(), mark.c_str());
            if (!phase) return nb::none();
            nb::dict result;
            result["phase_mark"] = std::string(phase->phase_mark);
            result["priority"] = phase->priority;
            result["state"] = shader_program_render_state_to_dict(phase->state);
            result["shader"] = TcShader(phase->shader);
            return result;
        })
        .def("__repr__", [](const TcShaderProgram& value) {
            if (!value.is_valid()) return std::string("<TcShaderProgram invalid>");
            return std::string("<TcShaderProgram ") + value.uuid() + " v"
                + std::to_string(value.version()) + ">";
        });

    // Shader registry info functions
    m.def("shader_count", []() { return tc_shader_count(); });
    m.def("shader_program_count", []() { return tc_shader_program_count(); });
    m.def("shader_program_get_all_info", []() {
        nb::list result;
        size_t count = 0;
        tc_shader_program_info* infos = tc_shader_program_get_all_info(&count);
        for (size_t i = 0; i < count; ++i) {
            nb::dict info;
            info["handle"] = nb::make_tuple(infos[i].handle.index, infos[i].handle.generation);
            info["uuid"] = std::string(infos[i].uuid);
            info["name"] = infos[i].name ? std::string(infos[i].name) : "";
            info["source_path"] = infos[i].source_path ? std::string(infos[i].source_path) : "";
            info["language"] = infos[i].language ? std::string(infos[i].language) : "";
            info["ref_count"] = infos[i].ref_count;
            info["version"] = infos[i].version;
            info["property_count"] = infos[i].property_count;
            info["phase_count"] = infos[i].phase_count;
            info["is_loaded"] = infos[i].is_loaded != 0;
            result.append(info);
        }
        free(infos);
        return result;
    });
    m.def("shader_get_all_info", []() {
        nb::list result;
        size_t count = 0;
        tc_shader_info* infos = tc_shader_get_all_info(&count);
        if (infos) {
            for (size_t i = 0; i < count; i++) {
                nb::dict info;
                info["uuid"] = std::string(infos[i].uuid);
                info["source_hash"] = std::string(infos[i].source_hash);
                info["name"] = infos[i].name ? std::string(infos[i].name) : "";
                info["source_path"] = infos[i].source_path ? std::string(infos[i].source_path) : "";
                info["ref_count"] = infos[i].ref_count;
                info["version"] = infos[i].version;
                info["features"] = infos[i].features;
                info["language"] = infos[i].language;
                info["artifact_policy"] = infos[i].artifact_policy;
                info["source_size"] = infos[i].source_size;
                info["is_variant"] = (bool)infos[i].is_variant;
                info["variant_op"] = (int)infos[i].variant_op;
                info["has_geometry"] = (bool)infos[i].has_geometry;
                result.append(info);
            }
            free(infos);
        }
        return result;
    });
}

} // namespace tgfx_bindings
