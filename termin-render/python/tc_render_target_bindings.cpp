// tc_render_target_bindings.cpp - Python bindings for tc_render_target
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <tcbase/tc_log.hpp>

extern "C" {
#include "render/tc_render_target.h"
#include "render/tc_render_target_pool.h"
#include "render/tc_pipeline_pool.h"
#include "core/tc_scene_pool.h"
#include "core/tc_scene.h"
#include "core/tc_component.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
#include "tc_value.h"
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
        .def_prop_rw("kind",
            [](const tc_render_target_handle& h) -> std::string {
                return tc_render_target_kind_to_string(tc_render_target_get_kind(h));
            },
            [](tc_render_target_handle& h, const std::string& kind) {
                tc_render_target_kind parsed;
                if (!tc_render_target_kind_from_string(kind.c_str(), &parsed)) {
                    tc::Log::error("[tc_render_target] unknown kind: %s", kind.c_str());
                    return;
                }
                tc_render_target_set_kind(h, parsed);
            })
        .def_prop_rw("width",
            [](const tc_render_target_handle& h) { return tc_render_target_get_width(h); },
            [](tc_render_target_handle& h, int w) { tc_render_target_set_width(h, w); })
        .def_prop_rw("height",
            [](const tc_render_target_handle& h) { return tc_render_target_get_height(h); },
            [](tc_render_target_handle& h, int v) { tc_render_target_set_height(h, v); })
        .def_prop_rw("dynamic_resolution",
            [](const tc_render_target_handle& h) { return tc_render_target_get_dynamic_resolution(h); },
            [](tc_render_target_handle& h, bool v) { tc_render_target_set_dynamic_resolution(h, v); })
        .def_prop_rw("color_format",
            [](const tc_render_target_handle& h) -> std::string {
                return tc_render_target_format_to_string(tc_render_target_get_color_format(h));
            },
            [](tc_render_target_handle& h, const std::string& v) {
                tc_texture_format fmt;
                if (!tc_render_target_format_from_string(v.c_str(), &fmt)) {
                    tc::Log::error("[tc_render_target] unknown color_format: %s", v.c_str());
                    return;
                }
                tc_render_target_set_color_format(h, fmt);
            })
        .def_prop_rw("depth_format",
            [](const tc_render_target_handle& h) -> std::string {
                return tc_render_target_format_to_string(tc_render_target_get_depth_format(h));
            },
            [](tc_render_target_handle& h, const std::string& v) {
                tc_texture_format fmt;
                if (!tc_render_target_format_from_string(v.c_str(), &fmt)) {
                    tc::Log::error("[tc_render_target] unknown depth_format: %s", v.c_str());
                    return;
                }
                tc_render_target_set_depth_format(h, fmt);
            })
        .def_prop_rw("clear_color_enabled",
            [](const tc_render_target_handle& h) {
                return tc_render_target_get_clear_color_enabled(h);
            },
            [](tc_render_target_handle& h, bool v) {
                tc_render_target_set_clear_color_enabled(h, v);
            })
        .def_prop_rw("clear_color_value",
            [](const tc_render_target_handle& h) {
                float color[4];
                tc_render_target_get_clear_color_value(h, color);
                return nb::make_tuple(color[0], color[1], color[2], color[3]);
            },
            [](tc_render_target_handle& h, nb::sequence value) {
                if (nb::len(value) < 4) {
                    throw nb::value_error("clear_color_value requires 4 values");
                }
                tc_render_target_set_clear_color_value(
                    h,
                    nb::cast<float>(value[0]),
                    nb::cast<float>(value[1]),
                    nb::cast<float>(value[2]),
                    nb::cast<float>(value[3]));
            })
        .def_prop_rw("clear_depth_enabled",
            [](const tc_render_target_handle& h) {
                return tc_render_target_get_clear_depth_enabled(h);
            },
            [](tc_render_target_handle& h, bool v) {
                tc_render_target_set_clear_depth_enabled(h, v);
            })
        .def_prop_rw("clear_depth_value",
            [](const tc_render_target_handle& h) {
                return tc_render_target_get_clear_depth_value(h);
            },
            [](tc_render_target_handle& h, float v) {
                tc_render_target_set_clear_depth_value(h, v);
            })
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
                nb::module_ m = nb::module_::import_("termin.scene._scene_native");
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
                } catch (...) {
                }
                nb::object handle_obj = scene_obj.attr("scene_handle")();
                tc_scene_handle s = nb::cast<tc_scene_handle>(handle_obj);
                tc_render_target_set_scene(h, s);
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
        .def_prop_rw("xr_origin",
            [](const tc_render_target_handle& h) -> nb::object {
                tc_component* c = tc_render_target_get_xr_origin(h);
                if (!c) return nb::none();
                if (c->native_language == TC_LANGUAGE_PYTHON && c->body) {
                    return nb::borrow<nb::object>(reinterpret_cast<PyObject*>(c->body));
                }
                if (c->kind == TC_CXX_COMPONENT) {
                    nb::module_ m = nb::module_::import_("termin.render_components._components_render_native");
                    return m.attr("XrOriginComponent").attr("_from_c_component_ptr")(reinterpret_cast<uintptr_t>(c));
                }
                return nb::none();
            },
            [](tc_render_target_handle& h, nb::object origin_obj) {
                if (origin_obj.is_none()) {
                    tc_render_target_set_xr_origin(h, nullptr);
                    return;
                }
                uintptr_t ptr = nb::cast<uintptr_t>(origin_obj.attr("c_component_ptr")());
                tc_render_target_set_xr_origin(h, reinterpret_cast<tc_component*>(ptr));
            },
            nb::arg().none())
        .def_prop_rw("pipeline",
            [](const tc_render_target_handle& h) -> nb::object {
                tc_pipeline_handle ph = tc_render_target_get_pipeline(h);
                if (!tc_pipeline_handle_valid(ph)) return nb::none();
                nb::module_ m = nb::module_::import_("termin.render_framework");
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
        .def_prop_rw("pipeline_params",
            [](const tc_render_target_handle& h) -> nb::object {
                const tc_value* v = tc_render_target_get_pipeline_params(h);
                if (!v || v->type != TC_VALUE_DICT) return nb::dict();
                nb::dict result;
                for (size_t i = 0; i < v->data.dict.count; i++) {
                    const char* key = v->data.dict.entries[i].key;
                    tc_value* val = v->data.dict.entries[i].value;
                    if (key && val && val->type == TC_VALUE_STRING && val->data.s) {
                        result[nb::str(key)] = nb::str(val->data.s);
                    }
                }
                return result;
            },
            [](tc_render_target_handle& h, nb::object obj) {
                if (obj.is_none()) {
                    tc_render_target_set_pipeline_params(h, nullptr);
                    return;
                }
                tc_value dict = tc_value_dict_new();
                if (nb::isinstance<nb::dict>(obj)) {
                    for (auto item : nb::cast<nb::dict>(obj)) {
                        std::string key = nb::cast<std::string>(nb::str(item.first));
                        std::string val = nb::cast<std::string>(nb::str(item.second));
                        tc_value v = tc_value_string(val.c_str());
                        tc_value_dict_set(&dict, key.c_str(), v);
                    }
                }
                tc_render_target_set_pipeline_params(h, &dict);
                tc_value_free(&dict);
            })

        // --- Owned tc_textures (Phase 6) -----------------------------------
        // `ensure_textures()` allocates the color + depth tc_textures on
        // first call (lazy). `color_texture` / `depth_texture` return
        // TcTexture instances that can be fed directly into
        // material.set_texture(...) for render-to-texture chains.
        .def("ensure_textures", [](tc_render_target_handle& h) {
            tc_render_target_ensure_textures(h);
        })
        .def_prop_ro("color_texture",
            [](const tc_render_target_handle& h) -> nb::object {
                tc_texture_handle t = tc_render_target_get_color_texture(h);
                if (tc_texture_handle_is_invalid(t)) return nb::none();
                nb::module_ tgfx = nb::module_::import_("tgfx._tgfx_native");
                return tgfx.attr("TcTexture").attr("from_handle")(
                    t.index, t.generation);
            })
        .def_prop_ro("depth_texture",
            [](const tc_render_target_handle& h) -> nb::object {
                tc_texture_handle t = tc_render_target_get_depth_texture(h);
                if (tc_texture_handle_is_invalid(t)) return nb::none();
                nb::module_ tgfx = nb::module_::import_("tgfx._tgfx_native");
                return tgfx.attr("TcTexture").attr("from_handle")(
                    t.index, t.generation);
            })
        .def("output_resource_info", [](tc_render_target_handle& h, const std::string& resource_name) -> nb::object {
            const bool fbo_resource = resource_name == "OUTPUT"
                || resource_name == "DISPLAY"
                || resource_name == "RT_COLOR";
            const bool color_texture_resource = resource_name == "RT_COLOR.color";
            const bool depth_texture_resource = resource_name == "RT_DEPTH"
                || resource_name == "RT_COLOR.depth";
            if (!fbo_resource && !color_texture_resource && !depth_texture_resource) {
                return nb::none();
            }

            tc_render_target_ensure_textures(h);

            tc_texture_handle color_handle = tc_render_target_get_color_texture(h);
            tc_texture_handle depth_handle = tc_render_target_get_depth_texture(h);
            tc_texture* color = tc_texture_get(color_handle);
            tc_texture* depth = tc_texture_get(depth_handle);
            tc_texture* primary = depth_texture_resource ? depth : color;
            if (!primary) {
                return nb::none();
            }

            nb::dict d;
            d["key"] = resource_name;
            d["width"] = static_cast<int>(primary->width);
            d["height"] = static_cast<int>(primary->height);
            d["samples"] = 1;
            if (depth_texture_resource) {
                d["resource_type"] = "depth_texture";
                d["has_depth"] = false;
                d["color_format_name"] = tc_render_target_format_to_string(
                    static_cast<tc_texture_format>(depth->format));
            } else if (color_texture_resource) {
                d["resource_type"] = "color_texture";
                d["has_depth"] = false;
                d["color_format_name"] = tc_render_target_format_to_string(
                    static_cast<tc_texture_format>(color->format));
            } else {
                d["resource_type"] = "fbo";
                d["has_depth"] = depth != nullptr;
                d["color_format_name"] = tc_render_target_format_to_string(
                    static_cast<tc_texture_format>(color->format));
            }
            if (fbo_resource && depth) {
                d["depth_format_name"] = tc_render_target_format_to_string(
                    static_cast<tc_texture_format>(depth->format));
            }
            return d;
        }, nb::arg("resource_name"))

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
