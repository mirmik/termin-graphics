// engine_bindings.cpp - PlotEngine2D + PlotEngine3D bindings.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <tcbase/input_enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/engine2d.hpp"
#include "tcplot/engine3d.hpp"

namespace nb = nanobind;

namespace tcplot_bindings {

namespace {

std::optional<tcplot::Color4> optional_color_from_obj(nb::object obj) {
    if (obj.is_none()) return std::nullopt;
    if (nb::isinstance<tcplot::Color4>(obj)) {
        return nb::cast<tcplot::Color4>(obj);
    }
    // Assume iterable of 3 or 4 floats.
    auto seq = nb::cast<nb::sequence>(obj);
    float c[4] = {0, 0, 0, 1};
    int i = 0;
    for (auto v : seq) {
        if (i >= 4) break;
        c[i++] = nb::cast<float>(v);
    }
    return tcplot::Color4{c[0], c[1], c[2], c[3]};
}

std::vector<double> vec_from_array(
    nb::ndarray<double, nb::c_contig, nb::device::cpu> arr) {
    return std::vector<double>(arr.data(), arr.data() + arr.size());
}

}  // namespace

void bind_engines(nb::module_& m) {
    // tcbase::MouseButton comes from tcbase._tcbase_native — don't
    // re-bind here. Importing it lazily below gives the engine's
    // mouse-event signatures a nanobind type registration to cast
    // against without forcing tcbase as a module-level dependency.
    {
        nb::module_ tcbase_native = nb::module_::import_("tcbase._tcbase_native");
        // Re-export for caller convenience: `tcplot.MouseButton`.
        m.attr("MouseButton") = tcbase_native.attr("MouseButton");
    }

    // ---- PickResult3D ----
    nb::class_<tcplot::PickResult3D>(m, "PickResult3D")
        .def_ro("x", &tcplot::PickResult3D::x)
        .def_ro("y", &tcplot::PickResult3D::y)
        .def_ro("z", &tcplot::PickResult3D::z)
        .def_ro("screen_dist_px", &tcplot::PickResult3D::screen_dist_px);

    // ---- PlotEngine2D ----
    nb::class_<tcplot::PlotEngine2D>(m, "PlotEngine2D")
        .def(nb::init<>())

        .def_rw("data", &tcplot::PlotEngine2D::data)

        // Style / margins
        .def_rw("margin_left",   &tcplot::PlotEngine2D::margin_left)
        .def_rw("margin_right",  &tcplot::PlotEngine2D::margin_right)
        .def_rw("margin_top",    &tcplot::PlotEngine2D::margin_top)
        .def_rw("margin_bottom", &tcplot::PlotEngine2D::margin_bottom)
        .def_rw("show_grid",     &tcplot::PlotEngine2D::show_grid)
        .def_rw("grid_color",    &tcplot::PlotEngine2D::grid_color)
        .def_rw("axis_color",    &tcplot::PlotEngine2D::axis_color)
        .def_rw("label_color",   &tcplot::PlotEngine2D::label_color)
        .def_rw("bg_color",      &tcplot::PlotEngine2D::bg_color)
        .def_rw("plot_bg_color", &tcplot::PlotEngine2D::plot_bg_color)
        .def_rw("font_size",       &tcplot::PlotEngine2D::font_size)
        .def_rw("title_font_size", &tcplot::PlotEngine2D::title_font_size)

        .def("set_viewport", &tcplot::PlotEngine2D::set_viewport,
             nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"))

        .def("plot",
             [](tcplot::PlotEngine2D& self,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                nb::object color, double thickness, const std::string& label) {
                 self.plot(vec_from_array(x), vec_from_array(y),
                           optional_color_from_obj(color), thickness, label);
             },
             nb::arg("x"), nb::arg("y"),
             nb::arg("color") = nb::none(),
             nb::arg("thickness") = 1.5,
             nb::arg("label") = std::string())

        .def("scatter",
             [](tcplot::PlotEngine2D& self,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                nb::object color, double size, const std::string& label) {
                 self.scatter(vec_from_array(x), vec_from_array(y),
                              optional_color_from_obj(color), size, label);
             },
             nb::arg("x"), nb::arg("y"),
             nb::arg("color") = nb::none(),
             nb::arg("size") = 4.0,
             nb::arg("label") = std::string())

        .def("clear",   &tcplot::PlotEngine2D::clear)
        .def("fit",     &tcplot::PlotEngine2D::fit)
        .def("set_view", &tcplot::PlotEngine2D::set_view,
             nb::arg("x_min"), nb::arg("x_max"),
             nb::arg("y_min"), nb::arg("y_max"))

        .def("render",
             [](tcplot::PlotEngine2D& self,
                tgfx::RenderContext2* ctx,
                tgfx::FontAtlas* font) {
                 self.render(ctx, font);
             },
             nb::arg("ctx"), nb::arg("font").none() = nb::none())

        .def("release_gpu_resources", &tcplot::PlotEngine2D::release_gpu_resources)

        // --- Input handlers ---
        .def("on_mouse_down",
             [](tcplot::PlotEngine2D& self, float x, float y, nb::object btn) {
                 tcbase::MouseButton b = nb::cast<tcbase::MouseButton>(btn);
                 return self.on_mouse_down(x, y, b);
             },
             nb::arg("x"), nb::arg("y"), nb::arg("button"))
        .def("on_mouse_move", &tcplot::PlotEngine2D::on_mouse_move,
             nb::arg("x"), nb::arg("y"))
        .def("on_mouse_up",
             [](tcplot::PlotEngine2D& self, float x, float y, nb::object btn) {
                 tcbase::MouseButton b = nb::cast<tcbase::MouseButton>(btn);
                 self.on_mouse_up(x, y, b);
             },
             nb::arg("x"), nb::arg("y"), nb::arg("button"))
        .def("on_mouse_wheel", &tcplot::PlotEngine2D::on_mouse_wheel,
             nb::arg("x"), nb::arg("y"), nb::arg("dy"));

    // ---- PlotEngine3D ----
    nb::class_<tcplot::PlotEngine3D>(m, "PlotEngine3D")
        .def(nb::init<>())

        .def_rw("data",   &tcplot::PlotEngine3D::data)
        .def_rw("camera", &tcplot::PlotEngine3D::camera)
        .def_rw("show_grid",      &tcplot::PlotEngine3D::show_grid)
        .def_rw("show_wireframe", &tcplot::PlotEngine3D::show_wireframe)
        .def_rw("z_scale",        &tcplot::PlotEngine3D::z_scale)
        .def_rw("marker_mode",    &tcplot::PlotEngine3D::marker_mode)

        .def("set_viewport", &tcplot::PlotEngine3D::set_viewport,
             nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"))

        .def("plot",
             [](tcplot::PlotEngine3D& self,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> z,
                nb::object color, double thickness, const std::string& label) {
                 self.plot(vec_from_array(x), vec_from_array(y),
                           vec_from_array(z),
                           optional_color_from_obj(color),
                           thickness, label);
             },
             nb::arg("x"), nb::arg("y"), nb::arg("z"),
             nb::arg("color") = nb::none(),
             nb::arg("thickness") = 1.5,
             nb::arg("label") = std::string())

        .def("scatter",
             [](tcplot::PlotEngine3D& self,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> z,
                nb::object color, double size, const std::string& label) {
                 self.scatter(vec_from_array(x), vec_from_array(y),
                              vec_from_array(z),
                              optional_color_from_obj(color),
                              size, label);
             },
             nb::arg("x"), nb::arg("y"), nb::arg("z"),
             nb::arg("color") = nb::none(),
             nb::arg("size") = 4.0,
             nb::arg("label") = std::string())

        // Surface takes flat X/Y/Z arrays plus explicit rows/cols.
        // The Python wrapper in tcplot/plot3d.py accepts 2D numpy
        // arrays, ravels them to 1D, and reads rows/cols from .shape
        // — that keeps ndarray-shape manipulation out of C++.
        .def("surface",
             [](tcplot::PlotEngine3D& self,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> X,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> Y,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> Z,
                uint32_t rows, uint32_t cols,
                nb::object color, bool wireframe, const std::string& label) {
                 self.surface(vec_from_array(X), vec_from_array(Y),
                              vec_from_array(Z),
                              rows, cols,
                              optional_color_from_obj(color),
                              wireframe, label);
             },
             nb::arg("X"), nb::arg("Y"), nb::arg("Z"),
             nb::arg("rows"), nb::arg("cols"),
             nb::arg("color") = nb::none(),
             nb::arg("wireframe") = false,
             nb::arg("label") = std::string())

        .def("clear",              &tcplot::PlotEngine3D::clear)
        .def("toggle_wireframe",   &tcplot::PlotEngine3D::toggle_wireframe)
        .def("toggle_marker_mode", &tcplot::PlotEngine3D::toggle_marker_mode)

        .def("render",
             [](tcplot::PlotEngine3D& self,
                tgfx::RenderContext2* ctx,
                tgfx::FontAtlas* font) {
                 self.render(ctx, font);
             },
             nb::arg("ctx"), nb::arg("font").none() = nb::none())

        .def("release_gpu_resources", &tcplot::PlotEngine3D::release_gpu_resources)

        .def("pick",
             [](const tcplot::PlotEngine3D& self, float mx, float my) -> nb::object {
                 auto r = self.pick(mx, my);
                 if (!r.has_value()) return nb::none();
                 return nb::make_tuple(r->x, r->y, r->z, r->screen_dist_px);
             },
             nb::arg("mx"), nb::arg("my"))

        .def("on_mouse_down",
             [](tcplot::PlotEngine3D& self, float x, float y, nb::object btn) {
                 tcbase::MouseButton b = nb::cast<tcbase::MouseButton>(btn);
                 return self.on_mouse_down(x, y, b);
             },
             nb::arg("x"), nb::arg("y"), nb::arg("button"))
        .def("on_mouse_move", &tcplot::PlotEngine3D::on_mouse_move,
             nb::arg("x"), nb::arg("y"))
        .def("on_mouse_up",
             [](tcplot::PlotEngine3D& self, float x, float y, nb::object btn) {
                 tcbase::MouseButton b = nb::cast<tcbase::MouseButton>(btn);
                 self.on_mouse_up(x, y, b);
             },
             nb::arg("x"), nb::arg("y"), nb::arg("button"))
        .def("on_mouse_wheel", &tcplot::PlotEngine3D::on_mouse_wheel,
             nb::arg("x"), nb::arg("y"), nb::arg("dy"));
}

}  // namespace tcplot_bindings
