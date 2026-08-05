// render_native_bindings.cpp - Drawable capability Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <tcbase/tc_log.hpp>
#include <termin/render/render_lifecycle.hpp>

extern "C" {
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"
#include "render/tc_render_category_flags.h"
#include "tc_component_python_drawable.h"
#include "tc_component_python_render_lifecycle.h"
#include "core/tc_component.h"
#include "tc_project_settings.h"
#include "tgfx/resources/tc_phase.h"
}

namespace nb = nanobind;

namespace {

PyObject* g_attachment_context_wrapper = nullptr;

nb::object wrap_attachment_context(const tc_render_attachment_context* context) {
    nb::object capsule = nb::steal<nb::object>(PyCapsule_New(
        const_cast<tc_render_attachment_context*>(context),
        "termin.RenderAttachmentContext",
        nullptr));
    if (!g_attachment_context_wrapper) return capsule;
    return nb::borrow<nb::object>(g_attachment_context_wrapper)(capsule);
}

void python_render_attach(
    void* py_self,
    const tc_render_attachment_context* context
) {
    PyGILState_STATE gil = PyGILState_Ensure();
    try {
        nb::handle self((PyObject*)py_self);
        self.attr("on_render_attach")(wrap_attachment_context(context));
    } catch (const std::exception& error) {
        tc::Log::error(error, "RenderLifecycleComponent::on_render_attach");
        PyErr_Print();
    }
    PyGILState_Release(gil);
}

void python_render_prepare(void* py_self, const tc_render_prepare_context* context) {
    PyGILState_STATE gil = PyGILState_Ensure();
    try {
        nb::handle self((PyObject*)py_self);
        const auto* cpp_context =
            reinterpret_cast<const termin::RenderPrepareContext*>(context);
        self.attr("prepare_render")(*cpp_context);
    } catch (const std::exception& error) {
        tc::Log::error(error, "RenderLifecycleComponent::prepare_render");
        PyErr_Print();
    }
    PyGILState_Release(gil);
}

void python_render_detach(
    void* py_self,
    const tc_render_attachment_context* context
) {
    PyGILState_STATE gil = PyGILState_Ensure();
    try {
        nb::handle self((PyObject*)py_self);
        self.attr("on_render_detach")(wrap_attachment_context(context));
    } catch (const std::exception& error) {
        tc::Log::error(error, "RenderLifecycleComponent::on_render_detach");
        PyErr_Print();
    }
    PyGILState_Release(gil);
}

} // namespace

namespace termin {
void bind_drawable(nb::module_& m);
void bind_render_configs(nb::module_& m);
void bind_scene_render_extensions(nb::module_& m);
}

NB_MODULE(_render_native, m) {
    nb::class_<termin::RenderPrepareContext>(m, "RenderPrepareContext")
        .def_prop_ro("scene_handle", &termin::RenderPrepareContext::scene);

    m.def("_set_render_attachment_context_wrapper", [](nb::object wrapper) {
        PyObject* replacement = wrapper.is_none() ? nullptr : wrapper.ptr();
        Py_XINCREF(replacement);
        Py_XDECREF(g_attachment_context_wrapper);
        g_attachment_context_wrapper = replacement;
    });

    tc_python_render_lifecycle_callbacks lifecycle_callbacks = {
        .on_render_attach = python_render_attach,
        .prepare_render = python_render_prepare,
        .on_render_detach = python_render_detach,
    };
    tc_component_set_python_render_lifecycle_callbacks(&lifecycle_callbacks);
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

    m.def("render_lifecycle_capability_id", []() {
        return tc_render_lifecycle_capability_id();
    }, "Get the render lifecycle capability ID");

    m.def("install_render_lifecycle", [](uintptr_t c_ptr) {
        auto* component = reinterpret_cast<tc_component*>(c_ptr);
        tc_component_install_python_render_lifecycle(component);
    }, nb::arg("c_ptr"), "Install render lifecycle capability on a component");

    m.def("render_lifecycle_priority", [](uintptr_t c_ptr) {
        return tc_render_lifecycle_priority(reinterpret_cast<tc_component*>(c_ptr));
    }, nb::arg("c_ptr"));

    m.def("set_render_lifecycle_priority", [](uintptr_t c_ptr, int priority) {
        if (!tc_render_lifecycle_set_priority(
                reinterpret_cast<tc_component*>(c_ptr), priority)) {
            throw nb::value_error("component has no render lifecycle capability");
        }
    }, nb::arg("c_ptr"), nb::arg("priority"));

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
