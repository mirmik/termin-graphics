// render_native_bindings.cpp - Drawable capability Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <tcbase/tc_log.hpp>
#include <termin/render/debug_geometry.hpp>
#include <termin/render/render_lifecycle.hpp>

extern "C" {
#include "core/tc_component.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"
#include "render/tc_render_category_flags.h"
#include "tc_component_python_drawable.h"
#include "tc_component_python_render_lifecycle.h"
#include "tc_project_settings.h"
#include "tgfx/resources/tc_phase.h"
}

namespace nb = nanobind;

namespace {

    PyObject* g_attachment_context_wrapper = nullptr;

    termin::Vec3 sequence_vec3(const nb::sequence& value, const char* label) {
        if (nb::len(value) != 3) {
            throw nb::value_error((std::string(label) + " must contain 3 values").c_str());
        }
        return {
            nb::cast<double>(value[0]),
            nb::cast<double>(value[1]),
            nb::cast<double>(value[2]),
        };
    }

    nb::object wrap_attachment_context(const tc_render_attachment_context* context) {
        nb::object capsule = nb::steal<nb::object>(PyCapsule_New(
            const_cast<tc_render_attachment_context*>(context), "termin.RenderAttachmentContext", nullptr));
        if (!g_attachment_context_wrapper)
            return capsule;
        return nb::borrow<nb::object>(g_attachment_context_wrapper)(capsule);
    }

    void python_render_attach(void* py_self, const tc_render_attachment_context* context) {
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
            const auto* cpp_context = reinterpret_cast<const termin::RenderPrepareContext*>(context);
            self.attr("prepare_render")(*cpp_context);
        } catch (const std::exception& error) {
            tc::Log::error(error, "RenderLifecycleComponent::prepare_render");
            PyErr_Print();
        }
        PyGILState_Release(gil);
    }

    void python_render_detach(void* py_self, const tc_render_attachment_context* context) {
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
} // namespace termin

NB_MODULE(_render_native, m) {
    nb::module_::import_("tcbase._geom_native");

    nb::class_<termin::DebugGeometryTypeRegistration>(m, "DebugGeometryTypeRegistration")
        .def(nb::init<const char*, const char*, const char*, bool>(),
             nb::arg("stable_id"),
             nb::arg("display_name"),
             nb::arg("category") = "",
             nb::arg("default_enabled") = true)
        .def_prop_ro("type_id", &termin::DebugGeometryTypeRegistration::type_id)
        .def_prop_ro("valid", &termin::DebugGeometryTypeRegistration::valid)
        .def("__bool__", &termin::DebugGeometryTypeRegistration::valid);

    nb::class_<termin::DebugGeometryDrawer>(m, "DebugGeometryDrawer")
        .def_prop_ro("valid", &termin::DebugGeometryDrawer::valid)
        .def("__bool__", &termin::DebugGeometryDrawer::valid)
        .def(
            "line",
            [](const termin::DebugGeometryDrawer& self,
               const nb::sequence& start,
               const nb::sequence& end,
               const termin::SrgbColor& color,
               bool depth_test) {
                return self.line(sequence_vec3(start, "start"), sequence_vec3(end, "end"), color, depth_test);
            },
            nb::arg("start"),
            nb::arg("end"),
            nb::arg("color"),
            nb::arg("depth_test") = false)
        .def(
            "wire_sphere",
            [](const termin::DebugGeometryDrawer& self,
               const nb::sequence& center,
               double radius,
               const termin::SrgbColor& color,
               int segments,
               bool depth_test) {
                return self.wire_sphere(sequence_vec3(center, "center"), radius, color, segments, depth_test);
            },
            nb::arg("center"),
            nb::arg("radius"),
            nb::arg("color"),
            nb::arg("segments") = 16,
            nb::arg("depth_test") = false)
        .def(
            "wire_box",
            [](const termin::DebugGeometryDrawer& self,
               const nb::sequence& center,
               const nb::sequence& half_axis_x,
               const nb::sequence& half_axis_y,
               const nb::sequence& half_axis_z,
               const termin::SrgbColor& color,
               bool depth_test) {
                return self.wire_box(sequence_vec3(center, "center"),
                                     sequence_vec3(half_axis_x, "half_axis_x"),
                                     sequence_vec3(half_axis_y, "half_axis_y"),
                                     sequence_vec3(half_axis_z, "half_axis_z"),
                                     color,
                                     depth_test);
            },
            nb::arg("center"),
            nb::arg("half_axis_x"),
            nb::arg("half_axis_y"),
            nb::arg("half_axis_z"),
            nb::arg("color"),
            nb::arg("depth_test") = false)
        .def(
            "wire_capsule",
            [](const termin::DebugGeometryDrawer& self,
               const nb::sequence& start,
               const nb::sequence& end,
               double radius,
               const termin::SrgbColor& color,
               int segments,
               bool depth_test) {
                return self.wire_capsule(sequence_vec3(start, "start"),
                                         sequence_vec3(end, "end"),
                                         radius,
                                         color,
                                         segments,
                                         depth_test);
            },
            nb::arg("start"),
            nb::arg("end"),
            nb::arg("radius"),
            nb::arg("color"),
            nb::arg("segments") = 16,
            nb::arg("depth_test") = false);

    nb::class_<termin::RenderPrepareContext>(m, "RenderPrepareContext")
        .def_prop_ro("scene_handle", &termin::RenderPrepareContext::scene)
        .def(
            "debug_geometry",
            [](const termin::RenderPrepareContext& self, nb::object type) {
                if (nb::isinstance<nb::str>(type)) {
                    std::string stable_id = nb::cast<std::string>(type);
                    return self.debug_geometry(stable_id.c_str());
                }
                return self.debug_geometry(nb::cast<tc_debug_geometry_type_id>(type));
            },
            nb::arg("type"));

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
    m.attr("RENDER_PHASE_EDITOR_DEBUG_TRANSPARENT") = nb::int_(TC_PHASE_EDITOR_DEBUG_TRANSPARENT);
    m.attr("RENDER_CATEGORY_NAVMESH") = nb::int_(TC_RENDER_CATEGORY_NAVMESH);
    m.attr("RENDER_CATEGORY_DEBUG_GEOMETRY") = nb::int_(TC_RENDER_CATEGORY_DEBUG_GEOMETRY);
    m.attr("RENDER_CATEGORY_ALL") = nb::int_(TC_RENDER_CATEGORY_ALL);

    nb::enum_<tc_render_sync_mode>(m, "RenderSyncMode")
        .value("NONE", TC_RENDER_SYNC_NONE)
        .value("FLUSH", TC_RENDER_SYNC_FLUSH)
        .value("FINISH", TC_RENDER_SYNC_FINISH);

    m.def(
        "get_render_sync_mode",
        []() { return tc_project_settings_get_render_sync_mode(); },
        "Get render sync mode between passes");

    m.def(
        "set_render_sync_mode",
        [](tc_render_sync_mode mode) { tc_project_settings_set_render_sync_mode(mode); },
        nb::arg("mode"),
        "Set render sync mode between passes");

    m.def(
        "configure_project_render_phases",
        [](const nb::sequence& names) {
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
        },
        nb::arg("names"),
        "Replace the indexed project render phase registry");

    m.def(
        "find_render_phase",
        [](const char* name) { return tc_phase_find(name); },
        nb::arg("name"),
        "Resolve a configured render phase name to its runtime bit");

    m.def("drawable_capability_id", []() { return tc_drawable_capability_id(); }, "Get the drawable capability ID");

    m.def(
        "render_lifecycle_capability_id",
        []() { return tc_render_lifecycle_capability_id(); },
        "Get the render lifecycle capability ID");

    m.def(
        "debug_geometry_types",
        []() {
            nb::list result;
            for (size_t index = 0; index < tc_debug_geometry_type_count(); ++index) {
                tc_debug_geometry_type_desc desc = {};
                if (!tc_debug_geometry_type_at(index, &desc))
                    continue;
                nb::dict item;
                item["type_id"] = desc.type_id;
                item["stable_id"] = desc.stable_id;
                item["display_name"] = desc.display_name;
                item["category"] = desc.category;
                item["default_enabled"] = desc.default_enabled;
                result.append(std::move(item));
            }
            return result;
        },
        "Enumerate registered debug geometry classes");

    m.def(
        "install_render_lifecycle",
        [](uintptr_t c_ptr) {
            auto* component = reinterpret_cast<tc_component*>(c_ptr);
            tc_component_install_python_render_lifecycle(component);
        },
        nb::arg("c_ptr"),
        "Install render lifecycle capability on a component");

    m.def(
        "render_lifecycle_priority",
        [](uintptr_t c_ptr) { return tc_render_lifecycle_priority(reinterpret_cast<tc_component*>(c_ptr)); },
        nb::arg("c_ptr"));

    m.def(
        "set_render_lifecycle_priority",
        [](uintptr_t c_ptr, int priority) {
            if (!tc_render_lifecycle_set_priority(reinterpret_cast<tc_component*>(c_ptr), priority)) {
                throw nb::value_error("component has no render lifecycle capability");
            }
        },
        nb::arg("c_ptr"),
        nb::arg("priority"));

    m.def(
        "install_drawable_vtable",
        [](uintptr_t c_ptr) {
            auto* c = reinterpret_cast<tc_component*>(c_ptr);
            if (c) {
                tc_component_install_python_drawable_vtable(c);
            }
        },
        nb::arg("c_ptr"),
        "Install drawable vtable on a component (by raw pointer)");

    m.def(
        "is_drawable",
        [](uintptr_t c_ptr) -> bool {
            auto* c = reinterpret_cast<tc_component*>(c_ptr);
            return c && tc_component_is_drawable(c);
        },
        nb::arg("c_ptr"),
        "Check if component is drawable");

    termin::bind_drawable(m);
    termin::bind_render_configs(m);
    termin::bind_scene_render_extensions(m);
}
