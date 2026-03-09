// shader_bindings.cpp - TcShader Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/ndarray.h>

#include "tgfx/tgfx_shader_handle.hpp"

namespace nb = nanobind;

using namespace termin;

namespace tgfx_bindings {

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
        .value("MORPHING", TC_SHADER_VARIANT_MORPHING);

    // Shader feature flags enum
    nb::enum_<tc_shader_feature>(m, "ShaderFeature")
        .value("NONE", TC_SHADER_FEATURE_NONE)
        .value("LIGHTING_UBO", TC_SHADER_FEATURE_LIGHTING_UBO);

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
        .def_prop_ro("has_geometry", &TcShader::has_geometry)
        .def_prop_ro("is_variant", &TcShader::is_variant)
        .def_prop_ro("variant_op", &TcShader::variant_op)
        .def_prop_ro("features", &TcShader::features)
        .def("has_feature", &TcShader::has_feature, nb::arg("feature"))
        .def("set_feature", &TcShader::set_feature, nb::arg("feature"))
        .def("set_features", &TcShader::set_features, nb::arg("features"))
        .def("variant_is_stale", &TcShader::variant_is_stale)
        .def("original", &TcShader::original)
        .def("set_variant_info", &TcShader::set_variant_info)
        .def_static("from_sources", &TcShader::from_sources,
            nb::arg("vertex"), nb::arg("fragment"),
            nb::arg("geometry") = "", nb::arg("name") = "", nb::arg("source_path") = "")
        .def_static("from_uuid", &TcShader::from_uuid)
        .def_static("from_hash", &TcShader::from_hash)
        .def_static("from_name", &TcShader::from_name)
        .def_static("get_or_create", &TcShader::get_or_create, nb::arg("uuid"),
            "Get existing tc_shader by UUID or create new one")
        .def("set_sources", &TcShader::set_sources,
            nb::arg("vertex"), nb::arg("fragment"),
            nb::arg("geometry") = "", nb::arg("name") = "", nb::arg("source_path") = "",
            "Set shader sources (bumps version if changed)")
        .def("__repr__", [](const TcShader& s) {
            if (!s.is_valid()) return std::string("<TcShader invalid>");
            std::string name = s.name();
            if (name.empty()) name = s.uuid();
            return "<TcShader " + name + " v" + std::to_string(s.version()) + ">";
        })
        // GL operations
        .def("compile", &TcShader::compile, "Compile shader if needed, returns GPU program ID")
        .def("use", &TcShader::use, "Use this shader (compiles if needed)")
        .def("ensure_ready", &TcShader::ensure_ready, "Compile shader if needed, returns True on success")
        .def_prop_ro("gpu_program", &TcShader::gpu_program, "Get GPU program ID (0 if not compiled)")
        // Uniform setters
        .def("set_uniform_int", &TcShader::set_uniform_int, nb::arg("name"), nb::arg("value"))
        .def("set_uniform_float", &TcShader::set_uniform_float, nb::arg("name"), nb::arg("value"))
        .def("set_uniform_vec2", &TcShader::set_uniform_vec2, nb::arg("name"), nb::arg("x"), nb::arg("y"))
        .def("set_uniform_vec2", [](TcShader& self, const char* name, nb::ndarray<nb::numpy, float, nb::shape<2>> v) {
            self.set_uniform_vec2(name, v(0), v(1));
        }, nb::arg("name"), nb::arg("v"))
        .def("set_uniform_vec3", &TcShader::set_uniform_vec3, nb::arg("name"), nb::arg("x"), nb::arg("y"), nb::arg("z"))
        .def("set_uniform_vec3", [](TcShader& self, const char* name, nb::ndarray<nb::numpy, float, nb::shape<3>> v) {
            self.set_uniform_vec3(name, v(0), v(1), v(2));
        }, nb::arg("name"), nb::arg("v"))
        .def("set_uniform_vec4", &TcShader::set_uniform_vec4, nb::arg("name"), nb::arg("x"), nb::arg("y"), nb::arg("z"), nb::arg("w"))
        .def("set_uniform_vec4", [](TcShader& self, const char* name, nb::ndarray<nb::numpy, float, nb::shape<4>> v) {
            self.set_uniform_vec4(name, v(0), v(1), v(2), v(3));
        }, nb::arg("name"), nb::arg("v"))
        .def("set_uniform_mat4", [](TcShader& self, const char* name, nb::ndarray<nb::numpy, float, nb::shape<4, 4>> m, bool transpose) {
            self.set_uniform_mat4(name, m.data(), transpose);
        }, nb::arg("name"), nb::arg("matrix"), nb::arg("transpose") = true)
        .def("set_uniform_mat4_array", [](TcShader& self, const char* name, nb::ndarray<nb::numpy, float> m, int count, bool transpose) {
            self.set_uniform_mat4_array(name, m.data(), count, transpose);
        }, nb::arg("name"), nb::arg("matrices"), nb::arg("count"), nb::arg("transpose") = true)
        .def("set_block_binding", &TcShader::set_block_binding, nb::arg("block_name"), nb::arg("binding_point"));

    // Shader registry info functions
    m.def("shader_count", []() { return tc_shader_count(); });
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
