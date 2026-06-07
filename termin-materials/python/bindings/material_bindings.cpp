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
extern "C" {
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_material_registry.h>
}

namespace termin {

namespace nb = nanobind;

namespace {

TcTexture require_tc_texture(nb::object value, const std::string& context) {
    if (nb::isinstance<TcTexture>(value)) {
        return nb::cast<TcTexture>(value);
    }
    tc::Log::error("%s expects TcTexture; TextureHandle must be resolved by the app layer", context.c_str());
    throw std::runtime_error(context + " expects TcTexture");
}

TcTexture optional_tc_texture(nb::object value, const std::string& context) {
    if (value.is_none()) {
        return TcTexture();
    }
    return require_tc_texture(value, context);
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

} // namespace


TcMaterial create_material_from_parsed(
    const ShaderMultyPhaseProgramm& program,
    nb::object color,
    nb::object textures,
    nb::object uniforms,
    nb::object name,
    nb::object source_path,
    const std::string& shader_uuid,
    nb::object default_white_texture,
    nb::object default_normal_texture
) {
    if (program.phases.empty()) {
        throw std::runtime_error("Program has no phases");
    }
    tc_shader_language language = shader_language_from_string(program.language);

    // Create material with uuid hint if provided
    TcMaterial mat = TcMaterial::create(
        name.is_none() ? program.program : nb::cast<std::string>(name),
        shader_uuid
    );
    if (!mat.is_valid()) {
        throw std::runtime_error("Failed to create TcMaterial");
    }

    mat.set_shader_name(program.program.c_str());
    if (!source_path.is_none()) {
        mat.set_source_path(nb::cast<std::string>(source_path).c_str());
    }

    TcTexture white_tex = optional_tc_texture(default_white_texture, "create_material_from_parsed(default_white_texture)");
    TcTexture normal_tex = optional_tc_texture(default_normal_texture, "create_material_from_parsed(default_normal_texture)");

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

        // Add phase
        tc_material_phase* phase = mat.add_phase_from_sources(
            vs.c_str(), fs.c_str(), gs.empty() ? nullptr : gs.c_str(),
            shader_name.c_str(),
            shader_phase.phase_mark.c_str(),
            shader_phase.priority,
            rs,
            nullptr,
            language
        );

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
                tc_shader_get(phase->shader),
                entries.data(),
                static_cast<uint32_t>(entries.size()),
                layout.block_size);
        } else {
            tc_shader_set_material_ubo_layout(tc_shader_get(phase->shader), nullptr, 0, 0);
        }

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
        if (!uniforms.is_none()) {
            nb::dict extras = nb::cast<nb::dict>(uniforms);
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
        if (!textures.is_none()) {
            nb::dict tex_dict = nb::cast<nb::dict>(textures);
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
        if (!color.is_none()) {
            if (nb::isinstance<Vec4>(color)) {
                Vec4 c = nb::cast<Vec4>(color);
                tc_material_phase_set_color(phase, c.x, c.y, c.z, c.w);
            } else if (nb::isinstance<nb::tuple>(color) || nb::isinstance<nb::list>(color)) {
                nb::sequence seq = nb::cast<nb::sequence>(color);
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
            } else if (nb::ndarray_check(value)) {
                nb::ndarray<nb::numpy, float> arr = nb::cast<nb::ndarray<nb::numpy, float>>(value);
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
                } else if (nb::ndarray_check(color_obj)) {
                    nb::ndarray<nb::numpy, float> arr = nb::cast<nb::ndarray<nb::numpy, float>>(color_obj);
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
            int language
        ) -> tc_material_phase* {
            tc_shader_language shader_language = static_cast<tc_shader_language>(language);
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
            return self.add_phase_from_sources(
                vs.c_str(),
                fs.c_str(),
                gs_ptr,
                shader_name.c_str(),
                phase_mark.c_str(),
                priority,
                state,
                shader_uuid.empty() ? nullptr : shader_uuid.c_str(),
                shader_language
            );
        }, nb::arg("vertex_source"), nb::arg("fragment_source"),
           nb::arg("geometry_source") = "", nb::arg("shader_name") = "",
           nb::arg("phase_mark") = "opaque", nb::arg("priority") = 0,
           nb::arg("state") = tc_render_state_opaque(),
           nb::arg("shader_uuid") = "",
           nb::arg("language") = static_cast<int>(TC_SHADER_LANGUAGE_GLSL),
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
        })
        // Legacy class entry point. Prefer module-level create_material_from_parsed().
        .def_static("from_parsed", &create_material_from_parsed,
            nb::arg("program"),
            nb::arg("color") = nb::none(),
            nb::arg("textures") = nb::none(),
            nb::arg("uniforms") = nb::none(),
            nb::arg("name") = nb::none(),
            nb::arg("source_path") = nb::none(),
            nb::arg("shader_uuid") = "",
            nb::arg("default_white_texture") = nb::none(),
            nb::arg("default_normal_texture") = nb::none());

    m.def("create_material_from_parsed", &create_material_from_parsed,
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
