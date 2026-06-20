#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>

#include <termin/render_target_config.hpp>
#include <termin/viewport_config.hpp>

namespace nb = nanobind;

namespace termin {

void bind_render_configs(nb::module_& m) {
    nb::class_<ViewportConfig>(m, "ViewportConfig")
        .def(nb::init<>())
        .def_rw("name", &ViewportConfig::name)
        .def_rw("display_name", &ViewportConfig::display_name)
        .def_rw("render_target_name", &ViewportConfig::render_target_name)
        .def_rw("region_x", &ViewportConfig::region_x)
        .def_rw("region_y", &ViewportConfig::region_y)
        .def_rw("region_w", &ViewportConfig::region_w)
        .def_rw("region_h", &ViewportConfig::region_h)
        .def_rw("depth", &ViewportConfig::depth)
        .def_rw("input_mode", &ViewportConfig::input_mode)
        .def_rw("block_input_in_editor", &ViewportConfig::block_input_in_editor)
        .def_rw("enabled", &ViewportConfig::enabled)
        .def_prop_ro("region", &ViewportConfig::region)
        .def("set_region", &ViewportConfig::set_region,
             nb::arg("x"), nb::arg("y"), nb::arg("w"), nb::arg("h"));

    nb::class_<RenderTargetConfig>(m, "RenderTargetConfig")
        .def(nb::init<>())
        .def_rw("name", &RenderTargetConfig::name)
        .def_rw("kind", &RenderTargetConfig::kind)
        .def_rw("camera_uuid", &RenderTargetConfig::camera_uuid)
        .def_rw("xr_origin_uuid", &RenderTargetConfig::xr_origin_uuid)
        .def_rw("width", &RenderTargetConfig::width)
        .def_rw("height", &RenderTargetConfig::height)
        .def_rw("dynamic_resolution", &RenderTargetConfig::dynamic_resolution)
        .def_rw("color_format", &RenderTargetConfig::color_format)
        .def_rw("depth_format", &RenderTargetConfig::depth_format)
        .def_rw("clear_color", &RenderTargetConfig::clear_color)
        .def_prop_rw("clear_color_value",
            [](const RenderTargetConfig& self) {
                return nb::make_tuple(
                    self.clear_color_value[0],
                    self.clear_color_value[1],
                    self.clear_color_value[2],
                    self.clear_color_value[3]);
            },
            [](RenderTargetConfig& self, nb::sequence value) {
                if (nb::len(value) < 4) {
                    throw nb::value_error("clear_color_value requires 4 values");
                }
                for (size_t i = 0; i < 4; i++) {
                    self.clear_color_value[i] = nb::cast<float>(value[i]);
                }
            })
        .def_rw("clear_depth", &RenderTargetConfig::clear_depth)
        .def_rw("clear_depth_value", &RenderTargetConfig::clear_depth_value)
        .def_rw("pipeline_uuid", &RenderTargetConfig::pipeline_uuid)
        .def_rw("pipeline_name", &RenderTargetConfig::pipeline_name)
        .def_rw("layer_mask", &RenderTargetConfig::layer_mask)
        .def_rw("enabled", &RenderTargetConfig::enabled)
        .def_rw("pipeline_params", &RenderTargetConfig::pipeline_params);
}

} // namespace termin
