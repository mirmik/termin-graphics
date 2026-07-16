#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <termin/render/scene_pipeline_template.hpp>
#include <termin/render/tc_scene_render_ext.hpp>
#include <termin/tc_scene.hpp>
#include <termin/geom/vec3.hpp>
#include <termin/geom/vec4.hpp>
#include <tgfx/tgfx_material_handle.hpp>
#include <tgfx/tgfx_mesh_handle.hpp>

extern "C" {
#include "core/tc_scene_extension.h"
#include "core/tc_scene_extension_ids.h"
#include "core/tc_scene_lighting.h"
#include "core/tc_scene_render_mount.h"
#include "core/tc_scene_render_state.h"
}

namespace nb = nanobind;

namespace termin {

namespace {

class SceneRenderState {
private:
    tc_scene_handle h_;

public:
    explicit SceneRenderState(tc_scene_handle h) : h_(h) {}

    tc_scene_handle handle() const {
        return h_;
    }

};

class SceneRenderMount {
private:
    tc_scene_handle h_;

public:
    explicit SceneRenderMount(tc_scene_handle h) : h_(h) {}

    tc_scene_handle handle() const {
        return h_;
    }

};

class TcSceneLighting {
private:
    tc_scene_lighting* ptr_ = nullptr;

public:
    explicit TcSceneLighting(uintptr_t ptr)
        : ptr_(reinterpret_cast<tc_scene_lighting*>(ptr)) {}

    explicit TcSceneLighting(tc_scene_lighting* ptr)
        : ptr_(ptr) {}

    bool valid() const {
        return ptr_ != nullptr;
    }

    std::tuple<float, float, float> ambient_color() const {
        if (!ptr_) return {1.0f, 1.0f, 1.0f};
        return {ptr_->ambient_color[0], ptr_->ambient_color[1], ptr_->ambient_color[2]};
    }

    void set_ambient_color(float r, float g, float b) {
        if (!ptr_) return;
        ptr_->ambient_color[0] = r;
        ptr_->ambient_color[1] = g;
        ptr_->ambient_color[2] = b;
    }

    float ambient_intensity() const {
        return ptr_ ? ptr_->ambient_intensity : 0.1f;
    }

    void set_ambient_intensity(float intensity) {
        if (ptr_) ptr_->ambient_intensity = intensity;
    }

    nb::object shadow_settings() const {
        if (!ptr_) return nb::none();
        nb::module_ lighting_module = nb::module_::import_("termin.lighting._lighting_native");
        nb::object cls = lighting_module.attr("ShadowSettings");
        return cls(ptr_->shadow_method, ptr_->shadow_softness, ptr_->shadow_bias);
    }

    void set_shadow_settings(nb::object value) {
        if (!ptr_) return;
        ptr_->shadow_method = nb::cast<int>(value.attr("method"));
        ptr_->shadow_softness = static_cast<float>(nb::cast<double>(value.attr("softness")));
        ptr_->shadow_bias = static_cast<float>(nb::cast<double>(value.attr("bias")));
    }

};

} // namespace

void bind_scene_render_extensions(nb::module_& m) {
    nb::module_::import_("tcbase._geom_native");
    nb::module_::import_("tgfx._tgfx_native");
    nb::module_::import_("termin.materials._materials_native");
    nb::module_::import_("termin.lighting._lighting_native");
    nb::module_::import_("termin.render_framework._render_framework_native");

    nb::class_<TcSceneLighting>(m, "TcSceneLighting",
        "View on scene lighting properties (ambient, shadows)")
        .def(nb::init<uintptr_t>(), nb::arg("ptr"))
        .def_prop_rw("ambient_color",
            [](TcSceneLighting& self) { return self.ambient_color(); },
            [](TcSceneLighting& self, std::tuple<float, float, float> color) {
                self.set_ambient_color(std::get<0>(color), std::get<1>(color), std::get<2>(color));
            },
            "Ambient light color (r, g, b)")
        .def_prop_rw("ambient_intensity",
            &TcSceneLighting::ambient_intensity,
            &TcSceneLighting::set_ambient_intensity,
            "Ambient light intensity")
        .def_prop_rw("shadow_settings",
            &TcSceneLighting::shadow_settings,
            &TcSceneLighting::set_shadow_settings,
            "Shadow rendering settings")
        .def("valid", &TcSceneLighting::valid,
            "Check if this lighting view is valid");

    nb::class_<SceneRenderState>(m, "SceneRenderState")
        .def_prop_rw("background_color",
            [](const SceneRenderState& self) -> Vec4 {
                return scene_background_color(TcSceneRef(self.handle()));
            },
            [](SceneRenderState& self, const Vec4& color) {
                scene_set_background_color(TcSceneRef(self.handle()), color);
            })
        .def("get_background_color", [](const SceneRenderState& self) {
            return scene_get_background_color(TcSceneRef(self.handle()));
        })
        .def("set_background_color", [](SceneRenderState& self, float r, float g, float b, float a) {
            scene_set_background_color(TcSceneRef(self.handle()), r, g, b, a);
        }, nb::arg("r"), nb::arg("g"), nb::arg("b"), nb::arg("a"))
        .def_prop_rw("skybox_type",
            [](const SceneRenderState& self) -> std::string {
                tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                int type = state ? state->skybox.type : TC_SKYBOX_GRADIENT;
                if (type == TC_SKYBOX_NONE) return "none";
                if (type == TC_SKYBOX_SOLID) return "solid";
                return "gradient";
            },
            [](SceneRenderState& self, const std::string& value) {
                int type = TC_SKYBOX_GRADIENT;
                if (value == "none") type = TC_SKYBOX_NONE;
                else if (value == "solid") type = TC_SKYBOX_SOLID;
                if (!tc_scene_render_state_ensure(self.handle())) return;
                tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                if (!state) return;
                state->skybox.type = type;
            })
        .def("get_skybox_type", [](const SceneRenderState& self) -> int {
            tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
            return state ? state->skybox.type : TC_SKYBOX_GRADIENT;
        })
        .def("set_skybox_type", [](SceneRenderState& self, int type) {
            if (!tc_scene_render_state_ensure(self.handle())) return;
            tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
            if (!state) return;
            state->skybox.type = type;
        })
        .def_prop_rw("skybox_color",
            [](const SceneRenderState& self) -> Vec3 {
                return scene_skybox_color(TcSceneRef(self.handle()));
            },
            [](SceneRenderState& self, const Vec3& color) {
                scene_set_skybox_color(TcSceneRef(self.handle()), color);
            })
        .def_prop_rw("skybox_top_color",
            [](const SceneRenderState& self) -> Vec3 {
                return scene_skybox_top_color(TcSceneRef(self.handle()));
            },
            [](SceneRenderState& self, const Vec3& color) {
                scene_set_skybox_top_color(TcSceneRef(self.handle()), color);
            })
        .def_prop_rw("skybox_bottom_color",
            [](const SceneRenderState& self) -> Vec3 {
                return scene_skybox_bottom_color(TcSceneRef(self.handle()));
            },
            [](SceneRenderState& self, const Vec3& color) {
                scene_set_skybox_bottom_color(TcSceneRef(self.handle()), color);
            })
        .def("get_skybox_color", [](const SceneRenderState& self) {
            return scene_get_skybox_color_components(TcSceneRef(self.handle()));
        })
        .def("set_skybox_color", [](SceneRenderState& self, float r, float g, float b) {
            scene_set_skybox_color_components(TcSceneRef(self.handle()), r, g, b);
        })
        .def("get_skybox_top_color", [](const SceneRenderState& self) {
            return scene_get_skybox_top_color_components(TcSceneRef(self.handle()));
        })
        .def("set_skybox_top_color", [](SceneRenderState& self, float r, float g, float b) {
            scene_set_skybox_top_color_components(TcSceneRef(self.handle()), r, g, b);
        })
        .def("get_skybox_bottom_color", [](const SceneRenderState& self) {
            return scene_get_skybox_bottom_color_components(TcSceneRef(self.handle()));
        })
        .def("set_skybox_bottom_color", [](SceneRenderState& self, float r, float g, float b) {
            scene_set_skybox_bottom_color_components(TcSceneRef(self.handle()), r, g, b);
        })
        .def("skybox_mesh", [](const SceneRenderState& self) -> TcMesh {
            tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
            tc_mesh* mesh = state ? tc_scene_skybox_ensure_mesh(&state->skybox) : nullptr;
            return mesh ? TcMesh(mesh) : TcMesh();
        })
        .def("skybox_material", [](const SceneRenderState& self) -> TcMaterial {
            tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
            tc_material* material = state ? tc_material_get(state->skybox.material) : nullptr;
            return TcMaterial(material);
        })
        .def_prop_rw("ambient_color",
            [](const SceneRenderState& self) -> Vec3 {
                return scene_ambient_color(TcSceneRef(self.handle()));
            },
            [](SceneRenderState& self, const Vec3& color) {
                scene_set_ambient_color(TcSceneRef(self.handle()), color);
            })
        .def_prop_rw("ambient_intensity",
            [](const SceneRenderState& self) -> float {
                return scene_ambient_intensity(TcSceneRef(self.handle()));
            },
            [](SceneRenderState& self, float intensity) {
                scene_set_ambient_intensity(TcSceneRef(self.handle()), intensity);
            })
        .def("lighting_ptr", [](const SceneRenderState& self) -> uintptr_t {
            return reinterpret_cast<uintptr_t>(scene_lighting(TcSceneRef(self.handle())));
        })
        .def("lighting", [](const SceneRenderState& self) -> nb::object {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(scene_lighting(TcSceneRef(self.handle())));
            if (ptr == 0) return nb::none();
            return nb::cast(TcSceneLighting(ptr));
        })
        .def_prop_rw("shadow_settings",
            [](const SceneRenderState& self) -> nb::object {
                tc_scene_lighting* lighting = scene_lighting(TcSceneRef(self.handle()));
                if (!lighting) return nb::none();
                nb::module_ lighting_module = nb::module_::import_("termin.lighting._lighting_native");
                nb::object cls = lighting_module.attr("ShadowSettings");
                return cls(lighting->shadow_method, lighting->shadow_softness, lighting->shadow_bias);
            },
            [](SceneRenderState& self, nb::object value) {
                tc_scene_lighting* lighting = scene_lighting(TcSceneRef(self.handle()));
                if (!lighting) return;
                lighting->shadow_method = nb::cast<int>(value.attr("method"));
                lighting->shadow_softness = static_cast<float>(nb::cast<double>(value.attr("softness")));
                lighting->shadow_bias = static_cast<float>(nb::cast<double>(value.attr("bias")));
            });

    nb::class_<SceneRenderMount>(m, "SceneRenderMount")
        .def("add_viewport_config", [](SceneRenderMount& self, const ViewportConfig& config) {
            scene_add_viewport_config(TcSceneRef(self.handle()), config);
        }, nb::arg("config"))
        .def("remove_viewport_config", [](SceneRenderMount& self, size_t index) {
            scene_remove_viewport_config(TcSceneRef(self.handle()), index);
        }, nb::arg("index"))
        .def("clear_viewport_configs", [](SceneRenderMount& self) {
            scene_clear_viewport_configs(TcSceneRef(self.handle()));
        })
        .def("viewport_config_count", [](const SceneRenderMount& self) {
            return scene_viewport_config_count(TcSceneRef(self.handle()));
        })
        .def("viewport_config_at", [](const SceneRenderMount& self, size_t index) -> nb::object {
            TcSceneRef scene(self.handle());
            if (index >= scene_viewport_config_count(scene)) return nb::none();
            return nb::cast(scene_viewport_config_at(scene, index));
        }, nb::arg("index"))
        .def_prop_ro("viewport_configs", [](const SceneRenderMount& self) {
            return scene_viewport_configs(TcSceneRef(self.handle()));
        })
        .def("add_render_target_config", [](SceneRenderMount& self, const RenderTargetConfig& config) {
            scene_add_render_target_config(TcSceneRef(self.handle()), config);
        }, nb::arg("config"))
        .def("remove_render_target_config", [](SceneRenderMount& self, size_t index) {
            scene_remove_render_target_config(TcSceneRef(self.handle()), index);
        }, nb::arg("index"))
        .def("clear_render_target_configs", [](SceneRenderMount& self) {
            scene_clear_render_target_configs(TcSceneRef(self.handle()));
        })
        .def("render_target_config_count", [](const SceneRenderMount& self) {
            return scene_render_target_config_count(TcSceneRef(self.handle()));
        })
        .def("render_target_config_at", [](const SceneRenderMount& self, size_t index) -> nb::object {
            TcSceneRef scene(self.handle());
            if (index >= scene_render_target_config_count(scene)) return nb::none();
            return nb::cast(scene_render_target_config_at(scene, index));
        }, nb::arg("index"))
        .def_prop_ro("render_target_configs", [](const SceneRenderMount& self) {
            return scene_render_target_configs(TcSceneRef(self.handle()));
        })
        .def("add_pipeline_template", [](SceneRenderMount& self, const TcScenePipelineTemplate& templ) {
            scene_add_pipeline_template(TcSceneRef(self.handle()), templ);
        }, nb::arg("template"))
        .def("remove_pipeline_template", [](SceneRenderMount& self, const TcScenePipelineTemplate& templ) {
            tc_scene_remove_pipeline_template(self.handle(), templ.handle());
        }, nb::arg("template"))
        .def("clear_pipeline_templates", [](SceneRenderMount& self) {
            scene_clear_pipeline_templates(TcSceneRef(self.handle()));
        })
        .def("pipeline_template_count", [](const SceneRenderMount& self) {
            return scene_pipeline_template_count(TcSceneRef(self.handle()));
        })
        .def("pipeline_template_at", [](const SceneRenderMount& self, size_t index) {
            return scene_pipeline_template_at(TcSceneRef(self.handle()), index);
        }, nb::arg("index"))
        .def_prop_ro("scene_pipelines", [](const SceneRenderMount& self) {
            TcSceneRef scene(self.handle());
            std::vector<TcScenePipelineTemplate> result;
            size_t count = scene_pipeline_template_count(scene);
            result.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                result.push_back(scene_pipeline_template_at(scene, i));
            }
            return result;
        });

    m.def("scene_render_state", [](const TcSceneRef& scene) {
        return SceneRenderState(scene.handle());
    }, nb::arg("scene"), "Get SceneRenderState extension from scene");

    m.def("scene_render_mount", [](const TcSceneRef& scene) {
        return SceneRenderMount(scene.handle());
    }, nb::arg("scene"), "Get SceneRenderMount extension from scene");

    m.attr("SCENE_EXT_TYPE_RENDER_MOUNT") = nb::int_(TC_SCENE_EXT_TYPE_RENDER_MOUNT);
    m.attr("SCENE_EXT_TYPE_RENDER_STATE") = nb::int_(TC_SCENE_EXT_TYPE_RENDER_STATE);
}

} // namespace termin
