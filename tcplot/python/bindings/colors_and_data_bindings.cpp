// colors_and_data_bindings.cpp - Color4 + series + PlotData bindings.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <optional>
#include <tuple>

#include "tcplot/plot_data.hpp"
#include "tcplot/styles.hpp"

namespace nb = nanobind;

namespace tcplot_bindings {

namespace {

// Color: accept any Python sequence of length 3 or 4 and turn it into
// a tcplot::Color4. Length-3 uses alpha = 1. Used by every series
// constructor and widget API call that takes an optional color.
tcplot::Color4 color_from_seq(nb::handle src) {
    if (src.is_none()) {
        return {};  // sentinel — caller checks via std::optional
    }
    if (nb::isinstance<tcplot::Color4>(src)) {
        return nb::cast<tcplot::Color4>(src);
    }
    // Accept iterable of 3 or 4 floats.
    auto seq = nb::cast<nb::sequence>(src);
    float c[4] = {0, 0, 0, 1};
    int i = 0;
    for (auto v : seq) {
        if (i >= 4) break;
        c[i++] = nb::cast<float>(v);
    }
    return {c[0], c[1], c[2], c[3]};
}

std::optional<tcplot::Color4> optional_color_from_obj(nb::object obj) {
    if (obj.is_none()) return std::nullopt;
    return color_from_seq(obj);
}

// Copy a numpy double array (or any iterable of floats) into a
// std::vector<double>. Used by add_line / add_scatter / surface.
std::vector<double> vec_from_array(
    nb::ndarray<double, nb::c_contig, nb::device::cpu> arr) {
    return std::vector<double>(arr.data(), arr.data() + arr.size());
}

}  // namespace

void bind_colors_and_data(nb::module_& m) {
    // ---- Color4 ----
    nb::class_<tcplot::Color4>(m, "Color4")
        .def(nb::init<>())
        .def(nb::init<float, float, float, float>(),
             nb::arg("r"), nb::arg("g"), nb::arg("b"), nb::arg("a") = 1.0f)
        .def_rw("r", &tcplot::Color4::r)
        .def_rw("g", &tcplot::Color4::g)
        .def_rw("b", &tcplot::Color4::b)
        .def_rw("a", &tcplot::Color4::a)
        // Convenience: convert to / from Python tuples.
        .def("as_tuple", [](const tcplot::Color4& c) {
            return std::make_tuple(c.r, c.g, c.b, c.a);
        });

    // ---- Palette helpers ----
    m.def("cycle_color", [](uint32_t index) {
        auto c = tcplot::styles::cycle_color(index);
        return std::make_tuple(c.r, c.g, c.b, c.a);
    }, nb::arg("index"));

    m.def("jet", [](float t) {
        auto c = tcplot::styles::jet(t);
        return std::make_tuple(c.r, c.g, c.b, c.a);
    }, nb::arg("t"));

    // Default colors as a list of 4-tuples (matches Python styles.DEFAULT_COLORS).
    m.def("default_colors", []() {
        std::vector<std::tuple<float, float, float, float>> out;
        out.reserve(tcplot::styles::default_colors_count());
        const tcplot::Color4* pal = tcplot::styles::default_colors();
        for (uint32_t i = 0; i < tcplot::styles::default_colors_count(); i++) {
            out.emplace_back(pal[i].r, pal[i].g, pal[i].b, pal[i].a);
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

        // add_line: accept numpy arrays for x/y/z and a tuple-or-None color.
        .def("add_line",
             [](tcplot::PlotData& self,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                nb::object color,
                double thickness,
                const std::string& label) {
                 self.add_line(vec_from_array(x), vec_from_array(y), {},
                               optional_color_from_obj(color),
                               thickness, label);
             },
             nb::arg("x"), nb::arg("y"),
             nb::arg("color") = nb::none(),
             nb::arg("thickness") = 1.5,
             nb::arg("label") = std::string())

        .def("add_scatter",
             [](tcplot::PlotData& self,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                nb::object color,
                double size,
                const std::string& label) {
                 self.add_scatter(vec_from_array(x), vec_from_array(y), {},
                                  optional_color_from_obj(color),
                                  size, label);
             },
             nb::arg("x"), nb::arg("y"),
             nb::arg("color") = nb::none(),
             nb::arg("size") = 4.0,
             nb::arg("label") = std::string())

        .def("data_bounds", [](const tcplot::PlotData& self) {
            auto b = self.data_bounds_2d();
            return std::make_tuple(b[0], b[1], b[2], b[3]);
        });
}

}  // namespace tcplot_bindings
