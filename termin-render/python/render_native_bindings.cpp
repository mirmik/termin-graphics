// render_native_bindings.cpp - Drawable capability Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

extern "C" {
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"
#include "render/tc_render_category_flags.h"
#include "tc_component_python_drawable.h"
#include "core/tc_component.h"
#include "tc_project_settings.h"
#include "tgfx/resources/tc_phase.h"
}

namespace nb = nanobind;

namespace termin {
void bind_drawable(nb::module_& m);
void bind_render_configs(nb::module_& m);
void bind_scene_render_extensions(nb::module_& m);
}

NB_MODULE(_render_native, m) {
    m.attr("PROJECT_RENDER_PHASE_CAPACITY") = nb::int_(TC_PHASE_PROJECT_CAPACITY);
    m.attr("RENDER_PHASE_NONE") = nb::int_(TC_PHASE_NONE);
    m.attr("RENDER_PHASE_OPAQUE") = nb::int_(TC_PHASE_OPAQUE);
    m.attr("RENDER_PHASE_TRANSPARENT") = nb::int_(TC_PHASE_TRANSPARENT);
    m.attr("RENDER_PHASE_NORMAL") = nb::int_(TC_PHASE_NORMAL);
    m.attr("RENDER_PHASE_DEPTH") = nb::int_(TC_PHASE_DEPTH);
    m.attr("RENDER_PHASE_ID") = nb::int_(TC_PHASE_ID);
    m.attr("RENDER_PHASE_SHADOW") = nb::int_(TC_PHASE_SHADOW);
    m.attr("RENDER_PHASE_UI") = nb::int_(TC_PHASE_UI);
    m.attr("RENDER_PHASE_EDITOR") = nb::int_(TC_PHASE_EDITOR);
    m.attr("RENDER_PHASE_EDITOR_DEBUG") = nb::int_(TC_PHASE_EDITOR_DEBUG);
    m.attr("RENDER_PHASE_EDITOR_DEBUG_TRANSPARENT") =
        nb::int_(TC_PHASE_EDITOR_DEBUG_TRANSPARENT);
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

    m.def("configure_project_render_phases", [](const nb::sequence& names) {
        if (nb::len(names) != TC_PHASE_PROJECT_CAPACITY) {
            throw nb::value_error("project render phase registry has invalid size");
        }
        tc_phase_clear_project_registry();
        for (uint32_t index = 0; index < TC_PHASE_PROJECT_CAPACITY; ++index) {
            std::string name = nb::cast<std::string>(names[index]);
            if (!tc_phase_set_project_name(index, name.c_str())) {
                tc_phase_clear_project_registry();
                throw nb::value_error("invalid project render phase registry");
            }
        }
    }, nb::arg("names"), "Replace the indexed project render phase registry");

    m.def("find_render_phase", [](const char* name) {
        return tc_phase_find(name);
    }, nb::arg("name"), "Resolve a configured render phase name to its runtime bit");

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
