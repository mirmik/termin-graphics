#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <optional>
#include <vector>

#include "tcplot/plot_data.hpp"

namespace tcplot_bindings {

namespace nb = nanobind;

inline tcplot::Color4 color_from_seq(nb::handle src) {
    if (src.is_none()) {
        return {};
    }
    if (nb::isinstance<tcplot::Color4>(src)) {
        return nb::cast<tcplot::Color4>(src);
    }
    auto seq = nb::cast<nb::sequence>(src);
    float c[4] = {0, 0, 0, 1};
    int i = 0;
    for (auto v : seq) {
        if (i >= 4) break;
        c[i++] = nb::cast<float>(v);
    }
    return {c[0], c[1], c[2], c[3]};
}

inline std::optional<tcplot::Color4> optional_color_from_obj(nb::object obj) {
    if (obj.is_none()) return std::nullopt;
    return color_from_seq(obj);
}

inline std::vector<double> vec_from_array(
    nb::ndarray<double, nb::c_contig, nb::device::cpu> arr
) {
    return std::vector<double>(arr.data(), arr.data() + arr.size());
}

} // namespace tcplot_bindings
