// colors_and_data_bindings.cpp - SrgbColor + series + PlotData bindings.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <tuple>

#include "conversion_helpers.hpp"
#include "tcplot/plot_data.hpp"
#include "tcplot/styles.hpp"

namespace nb = nanobind;

namespace tcplot_bindings {

    void bind_colors_and_data(nb::module_& m) {
        // Re-export the canonical base type so values cross module boundaries
        // without a competing nanobind registration.
        nb::module_ geom = nb::module_::import_("tcbase._geom_native");
        m.attr("SrgbColor") = geom.attr("SrgbColor");

        // ---- Palette helpers ----
        m.def(
            "cycle_color",
            [](uint32_t index) {
                auto c = tcplot::styles::cycle_color(index);
                return c;
            },
            nb::arg("index"));

        m.def(
            "jet",
            [](float t) {
                auto c = tcplot::styles::jet(t);
                return c;
            },
            nb::arg("t"));

        // Default colors as typed authored sRGB values.
        m.def("default_colors", []() {
            std::vector<tcplot::SrgbColor> out;
            out.reserve(tcplot::styles::default_colors_count());
            const tcplot::SrgbColor* pal = tcplot::styles::default_colors();
            for (uint32_t i = 0; i < tcplot::styles::default_colors_count(); i++) {
                out.push_back(pal[i]);
            }
            return out;
        });

        // ---- Series ----
        //
        // Series are exposed as mutable Python objects so callers can
        // tweak color/label after creation. Data vectors are returned /
        // accepted by value (copies cross the FFI boundary) — cheap for
        // the sizes we deal with (thousands of points at most).

        nb::class_<tcplot::LineSeries>(m, "LineSeries")
            .def(nb::init<>())
            .def_rw("x", &tcplot::LineSeries::x)
            .def_rw("y", &tcplot::LineSeries::y)
            .def_rw("z", &tcplot::LineSeries::z)
            .def_rw("color", &tcplot::LineSeries::color)
            .def_rw("thickness", &tcplot::LineSeries::thickness)
            .def_rw("label", &tcplot::LineSeries::label);

        nb::class_<tcplot::ScatterSeries>(m, "ScatterSeries")
            .def(nb::init<>())
            .def_rw("x", &tcplot::ScatterSeries::x)
            .def_rw("y", &tcplot::ScatterSeries::y)
            .def_rw("z", &tcplot::ScatterSeries::z)
            .def_rw("color", &tcplot::ScatterSeries::color)
            .def_rw("size", &tcplot::ScatterSeries::size)
            .def_rw("label", &tcplot::ScatterSeries::label);

        nb::class_<tcplot::SurfaceSeries>(m, "SurfaceSeries")
            .def(nb::init<>())
            .def_rw("X", &tcplot::SurfaceSeries::X)
            .def_rw("Y", &tcplot::SurfaceSeries::Y)
            .def_rw("Z", &tcplot::SurfaceSeries::Z)
            .def_rw("rows", &tcplot::SurfaceSeries::rows)
            .def_rw("cols", &tcplot::SurfaceSeries::cols)
            .def_rw("color", &tcplot::SurfaceSeries::color)
            .def_rw("wireframe", &tcplot::SurfaceSeries::wireframe)
            .def_rw("grid_visible", &tcplot::SurfaceSeries::grid_visible)
            .def_rw("grid_row_step", &tcplot::SurfaceSeries::grid_row_step)
            .def_rw("grid_col_step", &tcplot::SurfaceSeries::grid_col_step)
            .def_rw("grid_width_px", &tcplot::SurfaceSeries::grid_width_px)
            .def_rw("grid_color", &tcplot::SurfaceSeries::grid_color)
            .def_rw("label", &tcplot::SurfaceSeries::label);

        // ---- PlotData ----
        nb::class_<tcplot::PlotData>(m, "PlotData")
            .def(nb::init<>())
            .def_rw("lines", &tcplot::PlotData::lines)
            .def_rw("scatters", &tcplot::PlotData::scatters)
            .def_rw("surfaces", &tcplot::PlotData::surfaces)
            .def_rw("title", &tcplot::PlotData::title)
            .def_rw("x_label", &tcplot::PlotData::x_label)
            .def_rw("y_label", &tcplot::PlotData::y_label)
            .def_rw("z_label", &tcplot::PlotData::z_label)

            // add_line: accept numpy arrays for x/y/z and a typed-or-None color.
            .def(
                "add_line",
                [](tcplot::PlotData& self,
                   nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                   nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                   nb::object color,
                   double thickness,
                   const std::string& label) {
                    self.add_line(
                        vec_from_array(x), vec_from_array(y), {}, optional_color_from_obj(color), thickness, label);
                },
                nb::arg("x"),
                nb::arg("y"),
                nb::arg("color") = nb::none(),
                nb::arg("thickness") = 1.5,
                nb::arg("label") = std::string())

            .def(
                "add_scatter",
                [](tcplot::PlotData& self,
                   nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                   nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                   nb::object color,
                   double size,
                   const std::string& label) {
                    self.add_scatter(
                        vec_from_array(x), vec_from_array(y), {}, optional_color_from_obj(color), size, label);
                },
                nb::arg("x"),
                nb::arg("y"),
                nb::arg("color") = nb::none(),
                nb::arg("size") = 4.0,
                nb::arg("label") = std::string())

            .def("data_bounds", [](const tcplot::PlotData& self) {
                auto b = self.data_bounds_2d();
                return std::make_tuple(b[0], b[1], b[2], b[3]);
            });
    }

} // namespace tcplot_bindings
