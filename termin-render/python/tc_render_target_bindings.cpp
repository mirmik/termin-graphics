// tc_render_target_bindings.cpp - Python bindings for tc_render_target
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

extern "C" {
#include "render/tc_render_target.h"
#include "render/tc_render_target_pool.h"
#include "render/tc_pipeline_pool.h"
#include "core/tc_scene_pool.h"
#include "core/tc_scene.h"
#include "core/tc_component.h"
}

namespace nb = nanobind;

void bind_tc_render_target(nb::module_& m) {
    nb::class_<tc_pipeline_handle>(m, "PipelineHandle")
        .def(nb::init<>())
        .def_rw("index", &tc_pipeline_handle::index)
        .def_rw("generation", &tc_pipeline_handle::generation)
        .def_prop_ro("valid", [](const tc_pipeline_handle& h) { return tc_pipeline_handle_valid(h); });

    nb::class_<tc_render_target_handle>(m, "RenderTargetHandle")
        .def(nb::init<>())
        .def_rw("index", &tc_render_target_handle::index)
        .def_rw("generation", &tc_render_target_handle::generation)
        .def_prop_ro("valid", [](const tc_render_target_handle& h) {
            return tc_render_target_handle_valid(h);
        })
        .def_prop_ro("alive", [](const tc_render_target_handle& h) {
            return tc_render_target_alive(h);
        })
        .def_prop_rw("name",
            [](const tc_render_target_handle& h) -> std::string {
                const char* n = tc_render_target_get_name(h);
                return n ? n : "";
            },
            [](tc_render_target_handle& h, const std::string& name) {
                tc_render_target_set_name(h, name.c_str());
            })
        .def_prop_rw("width",
            [](const tc_render_target_handle& h) { return tc_render_target_get_width(h); },
            [](tc_render_target_handle& h, int w) { tc_render_target_set_width(h, w); })
        .def_prop_rw("height",
            [](const tc_render_target_handle& h) { return tc_render_target_get_height(h); },
            [](tc_render_target_handle& h, int v) { tc_render_target_set_height(h, v); })
        .def_prop_rw("enabled",
            [](const tc_render_target_handle& h) { return tc_render_target_get_enabled(h); },
            [](tc_render_target_handle& h, bool v) { tc_render_target_set_enabled(h, v); })
        .def_prop_rw("layer_mask",
            [](const tc_render_target_handle& h) { return tc_render_target_get_layer_mask(h); },
            [](tc_render_target_handle& h, uint64_t v) { tc_render_target_set_layer_mask(h, v); })
        .def_prop_rw("scene",
            [](const tc_render_target_handle& h) -> nb::object {
                tc_scene_handle s = tc_render_target_get_scene(h);
                if (!tc_scene_handle_valid(s) || !tc_scene_alive(s)) return nb::none();
                nb::module_ m = nb::module_::import_("termin.entity._entity_native");
                return m.attr("TcScene").attr("from_handle")(s.index, s.generation);
            },
            [](tc_render_target_handle& h, nb::object scene_obj) {
                if (scene_obj.is_none()) {
                    tc_render_target_set_scene(h, TC_SCENE_HANDLE_INVALID);
                    return;
                }
                // Accept tc_scene_handle directly (registered in _scene_native)
                try {
                    tc_scene_handle s = nb::cast<tc_scene_handle>(scene_obj);
                    tc_render_target_set_scene(h, s);
                    return;
                } catch (...) {}
                // Fallback: extract from scene_handle() method
                if (nb::hasattr(scene_obj, "scene_handle")) {
                    nb::object handle_obj = scene_obj.attr("scene_handle")();
                    tc_scene_handle s = nb::cast<tc_scene_handle>(handle_obj);
                    tc_render_target_set_scene(h, s);
                }
            },
            nb::arg().none())
        .def_prop_rw("camera",
            [](const tc_render_target_handle& h) -> nb::object {
                tc_component* c = tc_render_target_get_camera(h);
                if (!c) return nb::none();
                // Python component — return body directly
                if (c->native_language == TC_LANGUAGE_PYTHON && c->body) {
                    return nb::borrow<nb::object>(reinterpret_cast<PyObject*>(c->body));
                }
                // C++ CameraComponent
                if (c->kind == TC_CXX_COMPONENT) {
                    nb::module_ m = nb::module_::import_("termin.render_components._components_render_native");
                    return m.attr("CameraComponent").attr("_from_c_component_ptr")(reinterpret_cast<uintptr_t>(c));
                }
                return nb::none();
            },
            [](tc_render_target_handle& h, nb::object cam_obj) {
                if (cam_obj.is_none()) {
                    tc_render_target_set_camera(h, nullptr);
                    return;
                }
                uintptr_t ptr = nb::cast<uintptr_t>(cam_obj.attr("c_component_ptr")());
                tc_render_target_set_camera(h, reinterpret_cast<tc_component*>(ptr));
            },
            nb::arg().none())
        .def_prop_rw("pipeline",
            [](const tc_render_target_handle& h) -> nb::object {
                tc_pipeline_handle ph = tc_render_target_get_pipeline(h);
                if (!tc_pipeline_handle_valid(ph)) return nb::none();
                nb::module_ m = nb::module_::import_("termin._native.render");
                return m.attr("RenderPipeline").attr("from_handle")(ph.index, ph.generation);
            },
            [](tc_render_target_handle& h, nb::object pip_obj) {
                if (pip_obj.is_none()) {
                    tc_render_target_set_pipeline(h, TC_PIPELINE_HANDLE_INVALID);
                    return;
                }
                // RenderPipeline._pipeline_handle returns tc_pipeline_handle
                tc_pipeline_handle ph = nb::cast<tc_pipeline_handle>(pip_obj.attr("_pipeline_handle"));
                tc_render_target_set_pipeline(h, ph);
            },
            nb::arg().none())
        .def_prop_rw("locked",
            [](const tc_render_target_handle& h) { return tc_render_target_get_locked(h); },
            [](tc_render_target_handle& h, bool v) { tc_render_target_set_locked(h, v); })
        .def("free", [](tc_render_target_handle& h) { tc_render_target_free(h); });

    m.def("render_target_new", [](const std::string& name) {
        return tc_render_target_new(name.c_str());
    }, nb::arg("name"));

    m.def("render_target_pool_count", []() {
        return tc_render_target_pool_count();
    });

    m.def("render_target_pool_list", []() {
        std::vector<tc_render_target_handle> result;
        tc_render_target_pool_foreach([](tc_render_target_handle h, void* ud) -> bool {
            auto* vec = static_cast<std::vector<tc_render_target_handle>*>(ud);
            vec->push_back(h);
            return true;
        }, &result);
        return result;
    });
}
