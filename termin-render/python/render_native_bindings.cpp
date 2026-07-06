// render_native_bindings.cpp - Drawable capability Python bindings
#include <nanobind/nanobind.h>

extern "C" {
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"
#include "render/tc_render_category_flags.h"
#include "tc_component_python_drawable.h"
#include "core/tc_component.h"
#include "tc_project_settings.h"
}

namespace nb = nanobind;

namespace termin {
void bind_drawable(nb::module_& m);
void bind_render_configs(nb::module_& m);
void bind_scene_render_extensions(nb::module_& m);
}

NB_MODULE(_render_native, m) {
    m.attr("RENDER_CATEGORY_COLLIDERS") = nb::int_(TC_RENDER_CATEGORY_COLLIDERS);
    m.attr("RENDER_CATEGORY_NAVMESH") = nb::int_(TC_RENDER_CATEGORY_NAVMESH);
    m.attr("RENDER_CATEGORY_ALL") = nb::int_(TC_RENDER_CATEGORY_ALL);

    nb::enum_<tc_render_sync_mode>(m, "RenderSyncMode")
        .value("NONE", TC_RENDER_SYNC_NONE)
        .value("FLUSH", TC_RENDER_SYNC_FLUSH)
        .value("FINISH", TC_RENDER_SYNC_FINISH);

    m.def("get_render_sync_mode", []() {
        return tc_project_settings_get_render_sync_mode();
    }, "Get render sync mode between passes");

    m.def("set_render_sync_mode", [](tc_render_sync_mode mode) {
        tc_project_settings_set_render_sync_mode(mode);
    }, nb::arg("mode"), "Set render sync mode between passes");

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

    termin::bind_drawable(m);
    termin::bind_render_configs(m);
    termin::bind_scene_render_extensions(m);
}
