#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <optional>
#include <vector>

#include "tcplot/plot_data.hpp"

namespace tcplot_bindings {

    namespace nb = nanobind;

    inline tcplot::SrgbColor color_from_obj(nb::handle src) {
        if (src.is_none()) {
            return {};
        }
        if (nb::isinstance<tcplot::SrgbColor>(src)) {
            return nb::cast<tcplot::SrgbColor>(src);
        }
        throw nb::type_error("color must be tcplot.SrgbColor; RGBA tuples are not accepted");
    }

    inline std::optional<tcplot::SrgbColor> optional_color_from_obj(nb::object obj) {
        if (obj.is_none())
            return std::nullopt;
        return color_from_obj(obj);
    }

    inline std::vector<double> vec_from_array(nb::ndarray<double, nb::c_contig, nb::device::cpu> arr) {
        return std::vector<double>(arr.data(), arr.data() + arr.size());
    }

} // namespace tcplot_bindings
