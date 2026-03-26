// render_native_bindings.cpp - Drawable capability Python bindings
#include <nanobind/nanobind.h>

extern "C" {
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"
#include "tc_component_python_drawable.h"
#include "core/tc_component.h"
}

namespace nb = nanobind;

NB_MODULE(_render_native, m) {
    m.def("drawable_capability_id", []() {
        return tc_drawable_capability_id();
    }, "Get the drawable capability ID");

    m.def("install_drawable_vtable", [](uintptr_t c_ptr) {
        auto* c = reinterpret_cast<tc_component*>(c_ptr);
        if (c) {
            tc_component_install_python_drawable_vtable(c);
        }
    }, nb::arg("c_ptr"),
       "Install drawable vtable on a component (by raw pointer)");

    m.def("is_drawable", [](uintptr_t c_ptr) -> bool {
        auto* c = reinterpret_cast<tc_component*>(c_ptr);
        return c && tc_component_is_drawable(c);
    }, nb::arg("c_ptr"),
       "Check if component is drawable");
}
