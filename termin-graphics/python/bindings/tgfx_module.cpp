// tgfx_module.cpp - Main module for _tgfx_native Python bindings
#include <nanobind/nanobind.h>

namespace nb = nanobind;

namespace tgfx_bindings {
    void bind_types(nb::module_& m);
    void bind_render_state(nb::module_& m);
    void bind_shader(nb::module_& m);
    void bind_texture(nb::module_& m);
    void bind_tgfx2(nb::module_& m);
    void bind_immediate(nb::module_& m);
}

NB_MODULE(_tgfx_native, m) {
    m.doc() = "termin-graphics native Python bindings";

    nb::module_::import_("tcbase._tcbase_native");
    nb::module_::import_("tcbase._geom_native");

    tgfx_bindings::bind_types(m);
    tgfx_bindings::bind_render_state(m);
    tgfx_bindings::bind_shader(m);
    tgfx_bindings::bind_texture(m);
    tgfx_bindings::bind_tgfx2(m);
    tgfx_bindings::bind_immediate(m);

    // Import log from tcbase
    nb::module_ tcbase = nb::module_::import_("tcbase._tcbase_native");
    m.attr("log") = tcbase.attr("log");
}
