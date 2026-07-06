#include <nanobind/nanobind.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include <tcbase/tc_log.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/tc_scene.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

namespace nb = nanobind;

namespace termin {

void bind_render_engine(nb::module_& m) {
    nb::class_<RenderEngine>(m, "RenderEngine")
        .def(nb::init<>())
        .def("ensure_tgfx2", &RenderEngine::ensure_tgfx2)
        .def_prop_ro("tgfx2_ctx", &RenderEngine::tgfx2_ctx,
                     nb::rv_policy::reference_internal)
        .def_prop_ro("tgfx2_device", &RenderEngine::tgfx2_device,
                     nb::rv_policy::reference_internal)
        .def("render_scene_pipeline_offscreen", [](
            RenderEngine& self,
            RenderPipeline& pipeline,
            TcSceneRef scene_ref,
            const std::unordered_map<std::string, RenderTargetContext>& render_target_contexts,
            const std::string& default_render_target
        ) {
            std::vector<Light> lights;
            self.render_scene_pipeline_offscreen(
                pipeline,
                scene_ref.handle(),
                render_target_contexts,
                lights,
                default_render_target
            );
        },
             nb::arg("pipeline"),
             nb::arg("scene"),
             nb::arg("render_target_contexts"),
             nb::arg("default_render_target") = "");

    nb::class_<RenderTargetContext>(m, "RenderTargetContext")
        .def(nb::init<>())
        .def_rw("name", &RenderTargetContext::name)
        .def_rw("camera", &RenderTargetContext::camera)
        .def_rw("render_rect", &RenderTargetContext::render_rect)
        .def_rw("layer_mask", &RenderTargetContext::layer_mask)
        .def_rw("render_category_mask", &RenderTargetContext::render_category_mask)
        .def_rw("output_color_tex", &RenderTargetContext::output_color_tex)
        .def_rw("output_depth_tex", &RenderTargetContext::output_depth_tex)
        .def_rw("clear_color_enabled", &RenderTargetContext::clear_color_enabled)
        .def_rw("clear_depth_enabled", &RenderTargetContext::clear_depth_enabled)
        .def_rw("clear_depth", &RenderTargetContext::clear_depth)
        .def_rw("external_textures", &RenderTargetContext::external_textures);
}

} // namespace termin
