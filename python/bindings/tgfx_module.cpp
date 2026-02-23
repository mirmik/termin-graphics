// tgfx_module.cpp - Main module for _tgfx_native Python bindings
#include <nanobind/nanobind.h>

namespace nb = nanobind;

namespace tgfx_bindings {
    void bind_types(nb::module_& m);
    void bind_render_state(nb::module_& m);
    void bind_gpu_handles(nb::module_& m);
    void bind_graphics_backend(nb::module_& m);
    void bind_shader(nb::module_& m);
    void bind_texture(nb::module_& m);
    void bind_mesh(nb::module_& m);
}

NB_MODULE(_tgfx_native, m) {
    m.doc() = "termin-graphics native Python bindings";

    tgfx_bindings::bind_types(m);
    tgfx_bindings::bind_render_state(m);
    tgfx_bindings::bind_gpu_handles(m);
    tgfx_bindings::bind_graphics_backend(m);
    tgfx_bindings::bind_shader(m);
    tgfx_bindings::bind_texture(m);
    tgfx_bindings::bind_mesh(m);

    // Import log from tcbase
    nb::module_ tcbase = nb::module_::import_("tcbase._tcbase_native");
    m.attr("log") = tcbase.attr("log");
}
