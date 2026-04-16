// tcplot_module.cpp - Python bindings entry point for tcplot.

#include <nanobind/nanobind.h>

namespace nb = nanobind;

namespace tcplot_bindings {
    void bind_colors_and_data(nb::module_& m);
    void bind_camera(nb::module_& m);
    void bind_engines(nb::module_& m);
}

NB_MODULE(_tcplot_native, m) {
    m.doc() = "tcplot native Python bindings";

    tcplot_bindings::bind_colors_and_data(m);
    tcplot_bindings::bind_camera(m);
    tcplot_bindings::bind_engines(m);
}
