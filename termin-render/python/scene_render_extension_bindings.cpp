#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <stdexcept>

#include <termin/geom/color.hpp>
#include <termin/render/tc_pipeline_template.hpp>
#include <termin/render/tc_scene_render_ext.hpp>
#include <termin/tc_scene.hpp>
#include <tgfx/tgfx_material_handle.hpp>
#include <tgfx/tgfx_mesh_handle.hpp>

extern "C" {
#include "core/tc_debug_geometry.h"
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
            explicit SceneRenderState(tc_scene_handle h)
                : h_(h) {}

            tc_scene_handle handle() const {
                return h_;
            }
        };

        class SceneRenderMount {
        private:
            tc_scene_handle h_;

        public:
            explicit SceneRenderMount(tc_scene_handle h)
                : h_(h) {}

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

            SrgbColor ambient_srgb_color() const {
                if (!ptr_)
                    return SrgbColor::white();
                return {ptr_->ambient_color.r, ptr_->ambient_color.g, ptr_->ambient_color.b, 1.0f};
            }

            void set_ambient_srgb_color(SrgbColor color) {
                if (!ptr_)
                    return;
                ptr_->ambient_color = tc_srgb_color{color.r, color.g, color.b, 1.0f};
            }

            float ambient_intensity() const {
                return ptr_ ? ptr_->ambient_intensity : 0.1f;
            }

            void set_ambient_intensity(float intensity) {
                if (ptr_)
                    ptr_->ambient_intensity = intensity;
            }

            nb::object shadow_settings() const {
                if (!ptr_)
                    return nb::none();
                nb::module_ lighting_module = nb::module_::import_("termin.lighting._lighting_native");
                nb::object cls = lighting_module.attr("ShadowSettings");
                return cls(ptr_->shadow_method, ptr_->shadow_softness, ptr_->shadow_bias);
            }

            void set_shadow_settings(nb::object value) {
                if (!ptr_)
                    return;
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

        nb::class_<TcSceneLighting>(m, "TcSceneLighting", "View on scene lighting properties (ambient, shadows)")
            .def(nb::init<uintptr_t>(), nb::arg("ptr"))
            .def_prop_rw(
                "ambient_srgb_color",
                [](TcSceneLighting& self) { return self.ambient_srgb_color(); },
                [](TcSceneLighting& self, SrgbColor color) {
                    self.set_ambient_srgb_color(color);
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
            .def("valid", &TcSceneLighting::valid, "Check if this lighting view is valid");

        nb::class_<SceneRenderState>(m, "SceneRenderState")
            .def_prop_rw(
                "background_srgb_color",
                [](const SceneRenderState& self) -> SrgbColor { return scene_background_srgb_color(TcSceneRef(self.handle())); },
                [](SceneRenderState& self, SrgbColor color) {
                    scene_set_background_srgb_color(TcSceneRef(self.handle()), color);
                })
            .def("get_background_srgb_color",
                 [](const SceneRenderState& self) { return scene_background_srgb_color(TcSceneRef(self.handle())); })
            .def("set_background_srgb_color",
                 [](SceneRenderState& self, SrgbColor color) {
                     scene_set_background_srgb_color(TcSceneRef(self.handle()), color);
                 })
            .def_prop_rw(
                "skybox_type",
                [](const SceneRenderState& self) -> std::string {
                    tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                    int type = state ? state->skybox.type : TC_SKYBOX_GRADIENT;
                    if (type == TC_SKYBOX_NONE)
                        return "none";
                    if (type == TC_SKYBOX_SOLID)
                        return "solid";
                    return "gradient";
                },
                [](SceneRenderState& self, const std::string& value) {
                    int type = TC_SKYBOX_GRADIENT;
                    if (value == "none")
                        type = TC_SKYBOX_NONE;
                    else if (value == "solid")
                        type = TC_SKYBOX_SOLID;
                    if (!tc_scene_render_state_ensure(self.handle()))
                        return;
                    tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                    if (!state)
                        return;
                    state->skybox.type = type;
                })
            .def("get_skybox_type",
                 [](const SceneRenderState& self) -> int {
                     tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                     return state ? state->skybox.type : TC_SKYBOX_GRADIENT;
                 })
            .def("set_skybox_type",
                 [](SceneRenderState& self, int type) {
                     if (!tc_scene_render_state_ensure(self.handle()))
                         return;
                     tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                     if (!state)
                         return;
                     state->skybox.type = type;
                 })
            .def_prop_rw(
                "skybox_srgb_color",
                [](const SceneRenderState& self) -> SrgbColor { return scene_skybox_srgb_color(TcSceneRef(self.handle())); },
                [](SceneRenderState& self, SrgbColor color) {
                    scene_set_skybox_srgb_color(TcSceneRef(self.handle()), color);
                })
            .def_prop_rw(
                "skybox_top_srgb_color",
                [](const SceneRenderState& self) -> SrgbColor { return scene_skybox_top_srgb_color(TcSceneRef(self.handle())); },
                [](SceneRenderState& self, SrgbColor color) {
                    scene_set_skybox_top_srgb_color(TcSceneRef(self.handle()), color);
                })
            .def_prop_rw(
                "skybox_horizon_srgb_color",
                [](const SceneRenderState& self) -> SrgbColor {
                    return scene_skybox_horizon_srgb_color(TcSceneRef(self.handle()));
                },
                [](SceneRenderState& self, SrgbColor color) {
                    scene_set_skybox_horizon_srgb_color(TcSceneRef(self.handle()), color);
                })
            .def_prop_rw(
                "skybox_bottom_srgb_color",
                [](const SceneRenderState& self) -> SrgbColor {
                    return scene_skybox_bottom_srgb_color(TcSceneRef(self.handle()));
                },
                [](SceneRenderState& self, SrgbColor color) {
                    scene_set_skybox_bottom_srgb_color(TcSceneRef(self.handle()), color);
                })
            .def_prop_rw(
                "skybox_top_exponent",
                [](const SceneRenderState& self) { return scene_skybox_top_exponent(TcSceneRef(self.handle())); },
                [](SceneRenderState& self, float exponent) {
                    scene_set_skybox_top_exponent(TcSceneRef(self.handle()), exponent);
                })
            .def_prop_rw(
                "skybox_bottom_exponent",
                [](const SceneRenderState& self) { return scene_skybox_bottom_exponent(TcSceneRef(self.handle())); },
                [](SceneRenderState& self, float exponent) {
                    scene_set_skybox_bottom_exponent(TcSceneRef(self.handle()), exponent);
                })
            .def("skybox_mesh",
                 [](const SceneRenderState& self) -> TcMesh {
                     tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                     tc_mesh* mesh = state ? tc_scene_skybox_ensure_mesh(&state->skybox) : nullptr;
                     return mesh ? TcMesh(mesh) : TcMesh();
                 })
            .def("skybox_material",
                 [](const SceneRenderState& self) -> TcMaterial {
                     tc_scene_render_state* state = tc_scene_render_state_get(self.handle());
                     tc_material* material = state ? tc_material_get(state->skybox.material) : nullptr;
                     return TcMaterial(material);
                 })
            .def_prop_rw(
                "ambient_srgb_color",
                [](const SceneRenderState& self) -> SrgbColor { return scene_ambient_srgb_color(TcSceneRef(self.handle())); },
                [](SceneRenderState& self, SrgbColor color) {
                    scene_set_ambient_srgb_color(TcSceneRef(self.handle()), color);
                })
            .def_prop_rw(
                "ambient_intensity",
                [](const SceneRenderState& self) -> float {
                    return scene_ambient_intensity(TcSceneRef(self.handle()));
                },
                [](SceneRenderState& self, float intensity) {
                    scene_set_ambient_intensity(TcSceneRef(self.handle()), intensity);
                })
            .def("lighting_ptr",
                 [](const SceneRenderState& self) -> uintptr_t {
                     return reinterpret_cast<uintptr_t>(scene_lighting(TcSceneRef(self.handle())));
                 })
            .def("lighting",
                 [](const SceneRenderState& self) -> nb::object {
                     uintptr_t ptr = reinterpret_cast<uintptr_t>(scene_lighting(TcSceneRef(self.handle())));
                     if (ptr == 0)
                         return nb::none();
                     return nb::cast(TcSceneLighting(ptr));
                 })
            .def_prop_rw(
                "shadow_settings",
                [](const SceneRenderState& self) -> nb::object {
                    tc_scene_lighting* lighting = scene_lighting(TcSceneRef(self.handle()));
                    if (!lighting)
                        return nb::none();
                    nb::module_ lighting_module = nb::module_::import_("termin.lighting._lighting_native");
                    nb::object cls = lighting_module.attr("ShadowSettings");
                    return cls(lighting->shadow_method, lighting->shadow_softness, lighting->shadow_bias);
                },
                [](SceneRenderState& self, nb::object value) {
                    tc_scene_lighting* lighting = scene_lighting(TcSceneRef(self.handle()));
                    if (!lighting)
                        return;
                    lighting->shadow_method = nb::cast<int>(value.attr("method"));
                    lighting->shadow_softness = static_cast<float>(nb::cast<double>(value.attr("softness")));
                    lighting->shadow_bias = static_cast<float>(nb::cast<double>(value.attr("bias")));
                });

        nb::class_<SceneRenderMount>(m, "SceneRenderMount")
            .def(
                "debug_geometry_enabled",
                [](const SceneRenderMount& self, const std::string& stable_id) {
                    if (!tc_scene_render_mount_ensure(self.handle())) {
                        throw std::runtime_error("failed to attach scene render mount");
                    }
                    return tc_scene_debug_geometry_enabled(self.handle(),
                                                           tc_debug_geometry_type_find(stable_id.c_str()));
                },
                nb::arg("stable_id"))
            .def(
                "set_debug_geometry_enabled",
                [](SceneRenderMount& self, const std::string& stable_id, bool enabled) {
                    if (!tc_scene_render_mount_ensure(self.handle())) {
                        throw std::runtime_error("failed to attach scene render mount");
                    }
                    tc_debug_geometry_type_id type_id = tc_debug_geometry_type_find(stable_id.c_str());
                    if (!tc_scene_debug_geometry_set_enabled(self.handle(), type_id, enabled)) {
                        throw nb::value_error("unknown debug geometry type");
                    }
                },
                nb::arg("stable_id"),
                nb::arg("enabled"))
            .def(
                "add_viewport_config",
                [](SceneRenderMount& self, const ViewportConfig& config) {
                    scene_add_viewport_config(TcSceneRef(self.handle()), config);
                },
                nb::arg("config"))
            .def(
                "remove_viewport_config",
                [](SceneRenderMount& self, size_t index) {
                    scene_remove_viewport_config(TcSceneRef(self.handle()), index);
                },
                nb::arg("index"))
            .def("clear_viewport_configs",
                 [](SceneRenderMount& self) { scene_clear_viewport_configs(TcSceneRef(self.handle())); })
            .def("viewport_config_count",
                 [](const SceneRenderMount& self) { return scene_viewport_config_count(TcSceneRef(self.handle())); })
            .def(
                "viewport_config_at",
                [](const SceneRenderMount& self, size_t index) -> nb::object {
                    TcSceneRef scene(self.handle());
                    if (index >= scene_viewport_config_count(scene))
                        return nb::none();
                    return nb::cast(scene_viewport_config_at(scene, index));
                },
                nb::arg("index"))
            .def_prop_ro("viewport_configs",
                         [](const SceneRenderMount& self) { return scene_viewport_configs(TcSceneRef(self.handle())); })
            .def(
                "add_render_target_config",
                [](SceneRenderMount& self, const RenderTargetConfig& config) {
                    scene_add_render_target_config(TcSceneRef(self.handle()), config);
                },
                nb::arg("config"))
            .def(
                "remove_render_target_config",
                [](SceneRenderMount& self, size_t index) {
                    scene_remove_render_target_config(TcSceneRef(self.handle()), index);
                },
                nb::arg("index"))
            .def("clear_render_target_configs",
                 [](SceneRenderMount& self) { scene_clear_render_target_configs(TcSceneRef(self.handle())); })
            .def("render_target_config_count",
                 [](const SceneRenderMount& self) {
                     return scene_render_target_config_count(TcSceneRef(self.handle()));
                 })
            .def(
                "render_target_config_at",
                [](const SceneRenderMount& self, size_t index) -> nb::object {
                    TcSceneRef scene(self.handle());
                    if (index >= scene_render_target_config_count(scene))
                        return nb::none();
                    return nb::cast(scene_render_target_config_at(scene, index));
                },
                nb::arg("index"))
            .def_prop_ro(
                "render_target_configs",
                [](const SceneRenderMount& self) { return scene_render_target_configs(TcSceneRef(self.handle())); })
            .def(
                "add_pipeline_template",
                [](SceneRenderMount& self, const TcPipelineTemplate& pipeline_template) {
                    return scene_add_pipeline_template(TcSceneRef(self.handle()), pipeline_template);
                },
                nb::arg("pipeline_template"))
            .def(
                "remove_pipeline_template",
                [](SceneRenderMount& self, const TcPipelineTemplate& pipeline_template) {
                    return tc_scene_remove_pipeline_template(self.handle(), pipeline_template.handle);
                },
                nb::arg("pipeline_template"))
            .def("clear_pipeline_templates",
                 [](SceneRenderMount& self) { scene_clear_pipeline_templates(TcSceneRef(self.handle())); })
            .def("pipeline_template_count",
                 [](const SceneRenderMount& self) { return scene_pipeline_template_count(TcSceneRef(self.handle())); })
            .def(
                "pipeline_template_at",
                [](const SceneRenderMount& self, size_t index) {
                    return scene_pipeline_template_at(TcSceneRef(self.handle()), index);
                },
                nb::arg("index"))
            .def_prop_ro("pipeline_templates", [](const SceneRenderMount& self) {
                TcSceneRef scene(self.handle());
                std::vector<TcPipelineTemplate> result;
                size_t count = scene_pipeline_template_count(scene);
                result.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    result.push_back(scene_pipeline_template_at(scene, i));
                }
                return result;
            });

        m.def(
            "scene_render_state",
            [](const TcSceneRef& scene) { return SceneRenderState(scene.handle()); },
            nb::arg("scene"),
            "Get SceneRenderState extension from scene");

        m.def(
            "scene_render_mount",
            [](const TcSceneRef& scene) { return SceneRenderMount(scene.handle()); },
            nb::arg("scene"),
            "Get SceneRenderMount extension from scene");

        m.attr("SCENE_EXT_TYPE_RENDER_MOUNT") = nb::int_(TC_SCENE_EXT_TYPE_RENDER_MOUNT);
        m.attr("SCENE_EXT_TYPE_RENDER_STATE") = nb::int_(TC_SCENE_EXT_TYPE_RENDER_STATE);
    }

} // namespace termin
