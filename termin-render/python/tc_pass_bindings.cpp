#include <nanobind/nanobind.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

extern "C" {
#include "render/tc_frame_graph.h"
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
#include "tc_pipeline_registry.h"
#include "inspect/tc_inspect.h"
#include <tcbase/tc_log.h>
}

#include "inspect/tc_inspect_python.hpp"
#include "termin/bindings/tc_value_helpers.hpp"
#include "termin/render/execute_context.hpp"
#include "termin/render/frame_graph_debugger_core.hpp"
#include "termin/render/frame_pass.hpp"
#include "termin/render/resource_spec.hpp"
#include "termin/render/tc_pass.hpp"

namespace nb = nanobind;

namespace termin {

static std::unordered_map<std::string, std::shared_ptr<nb::object>>& python_pass_classes() {
    static std::unordered_map<std::string, std::shared_ptr<nb::object>> classes;
    return classes;
}

static tc_pass* python_pass_factory(void* userdata) {
    const char* type_name = static_cast<const char*>(userdata);

    auto& py_classes = python_pass_classes();
    auto it = py_classes.find(type_name);
    if (it == py_classes.end()) {
        tc_log(TC_LOG_ERROR, "python_pass_factory: class not found for type %s", type_name);
        return nullptr;
    }

    try {
        nb::object py_obj = (*(it->second))();

        if (nb::hasattr(py_obj, "_tc_pass")) {
            nb::object tc_pass_ref_obj = py_obj.attr("_tc_pass");
            if (nb::isinstance<TcPassRef>(tc_pass_ref_obj)) {
                TcPassRef ref = nb::cast<TcPassRef>(tc_pass_ref_obj);
                tc_pass* p = ref.ptr();
                if (p) {
                    Py_INCREF(py_obj.ptr());
                    return p;
                }
            }
        }

        tc_log(TC_LOG_ERROR, "python_pass_factory: %s has no valid _tc_pass", type_name);
    } catch (const nb::python_error& e) {
        tc_log(TC_LOG_ERROR, "python_pass_factory: failed to create %s: %s", type_name, e.what());
        PyErr_Clear();
    }

    return nullptr;
}

inline nb::object tc_pass_to_python(tc_pass* p) {
    if (!p) {
        return nb::none();
    }

    if (p->kind != TC_NATIVE_PASS && p->kind != TC_EXTERNAL_PASS) {
        tc_log(TC_LOG_ERROR, "[tc_pass_to_python] Invalid kind=%d for p=%p", (int) p->kind, (void*) p);
        return nb::none();
    }

    if (p->body && p->native_language == TC_LANGUAGE_PYTHON) {
        return nb::borrow<nb::object>(reinterpret_cast<PyObject*>(p->body));
    }

    if (p->kind == TC_NATIVE_PASS) {
        CxxFramePass* fp = CxxFramePass::from_tc(p);
        if (fp) {
            return nb::cast(fp, nb::rv_policy::reference);
        }
    }

    return nb::none();
}

static void py_pass_execute(void* wrapper, void* ctx) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        ExecuteContext* exec_ctx = static_cast<ExecuteContext*>(ctx);
        if (nb::hasattr(py_pass, "execute")) {
            py_pass.attr("execute")(nb::cast(exec_ctx, nb::rv_policy::reference));
        }
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python execute failed: %s", e.what());
    }
}

static size_t py_pass_get_reads(void* wrapper, const char** out, size_t max) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        if (!nb::hasattr(py_pass, "compute_reads")) {
            return 0;
        }

        nb::object reads = py_pass.attr("compute_reads")();
        size_t count = 0;
        nb::list cached;
        for (auto item : reads) {
            if (count >= max) {
                break;
            }
            nb::str s = nb::cast<nb::str>(item);
            cached.append(s);
            count++;
        }
        py_pass.attr("_cached_tc_reads") = cached;

        count = 0;
        for (auto item : cached) {
            if (count >= max) {
                break;
            }
            out[count] = PyUnicode_AsUTF8(item.ptr());
            count++;
        }
        return count;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python get_reads failed: %s", e.what());
        return 0;
    }
}

static size_t py_pass_get_writes(void* wrapper, const char** out, size_t max) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        if (!nb::hasattr(py_pass, "compute_writes")) {
            return 0;
        }

        nb::object writes = py_pass.attr("compute_writes")();
        size_t count = 0;
        nb::list cached;
        for (auto item : writes) {
            if (count >= max) {
                break;
            }
            nb::str s = nb::cast<nb::str>(item);
            cached.append(s);
            count++;
        }
        py_pass.attr("_cached_tc_writes") = cached;

        count = 0;
        for (auto item : cached) {
            if (count >= max) {
                break;
            }
            out[count] = PyUnicode_AsUTF8(item.ptr());
            count++;
        }
        return count;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python get_writes failed: %s", e.what());
        return 0;
    }
}

static size_t py_pass_get_inplace_aliases(void* wrapper, const char** out, size_t max) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        if (!nb::hasattr(py_pass, "get_inplace_aliases")) {
            return 0;
        }

        nb::object aliases = py_pass.attr("get_inplace_aliases")();
        size_t pair_count = 0;
        nb::list cached;
        for (auto item : aliases) {
            if (pair_count >= max) {
                break;
            }
            nb::tuple pair = nb::cast<nb::tuple>(item);
            cached.append(pair[0]);
            cached.append(pair[1]);
            pair_count++;
        }
        py_pass.attr("_cached_tc_aliases") = cached;

        size_t i = 0;
        for (auto item : cached) {
            out[i] = PyUnicode_AsUTF8(item.ptr());
            i++;
        }
        return pair_count;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python get_inplace_aliases failed: %s", e.what());
        return 0;
    }
}

static size_t py_pass_get_resource_specs(void* wrapper, void* out, size_t max) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        if (!nb::hasattr(py_pass, "get_resource_specs")) {
            return 0;
        }

        nb::object specs = py_pass.attr("get_resource_specs")();
        ResourceSpec* out_specs = static_cast<ResourceSpec*>(out);
        size_t count = 0;

        for (auto item : specs) {
            if (count >= max) {
                break;
            }

            nb::object spec = nb::borrow<nb::object>(item);
            ResourceSpec& s = out_specs[count];
            s = ResourceSpec();
            s.resource = nb::cast<std::string>(spec.attr("resource"));

            if (nb::hasattr(spec, "resource_type") && !spec.attr("resource_type").is_none()) {
                s.resource_type = nb::cast<std::string>(spec.attr("resource_type"));
            } else {
                s.resource_type = "fbo";
            }

            if (nb::hasattr(spec, "size") && !spec.attr("size").is_none()) {
                nb::tuple sz = nb::cast<nb::tuple>(spec.attr("size"));
                s.size = std::make_pair(nb::cast<int>(sz[0]), nb::cast<int>(sz[1]));
            }

            if (nb::hasattr(spec, "samples")) {
                s.samples = nb::cast<int>(spec.attr("samples"));
            }

            if (nb::hasattr(spec, "clear_color") && !spec.attr("clear_color").is_none()) {
                nb::tuple cc = nb::cast<nb::tuple>(spec.attr("clear_color"));
                s.clear_color = std::array<double, 4>{
                    nb::cast<double>(cc[0]), nb::cast<double>(cc[1]),
                    nb::cast<double>(cc[2]), nb::cast<double>(cc[3])
                };
            }
            if (nb::hasattr(spec, "clear_depth") && !spec.attr("clear_depth").is_none()) {
                s.clear_depth = nb::cast<float>(spec.attr("clear_depth"));
            }
            if (nb::hasattr(spec, "format") && !spec.attr("format").is_none()) {
                s.format = nb::cast<std::string>(spec.attr("format"));
            }

            count++;
        }

        return count;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python get_resource_specs failed: %s", e.what());
        return 0;
    }
}

static size_t py_pass_get_internal_symbols(void* wrapper, const char** out, size_t max) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        if (!nb::hasattr(py_pass, "get_internal_symbols")) {
            return 0;
        }

        nb::object symbols = py_pass.attr("get_internal_symbols")();
        size_t count = 0;
        nb::list cached;
        for (auto item : symbols) {
            if (count >= max) {
                break;
            }
            nb::str s = nb::cast<nb::str>(item);
            cached.append(s);
            count++;
        }
        py_pass.attr("_cached_tc_symbols") = cached;

        count = 0;
        for (auto item : cached) {
            if (count >= max) {
                break;
            }
            out[count] = PyUnicode_AsUTF8(item.ptr());
            count++;
        }
        return count;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python get_internal_symbols failed: %s", e.what());
        return 0;
    }
}

static void py_pass_destroy(void* wrapper) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        if (nb::hasattr(py_pass, "destroy")) {
            py_pass.attr("destroy")();
        }
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python destroy failed: %s", e.what());
    }
}

static tc_external_pass_callbacks g_py_pass_callbacks = {
    .execute = py_pass_execute,
    .get_reads = py_pass_get_reads,
    .get_writes = py_pass_get_writes,
    .get_inplace_aliases = py_pass_get_inplace_aliases,
    .get_resource_specs = py_pass_get_resource_specs,
    .get_internal_symbols = py_pass_get_internal_symbols,
    .destroy = py_pass_destroy,
};

static void py_pass_ref_retain(tc_pass* p) {
    if (p && p->body) {
        nb::gil_scoped_acquire gil;
        Py_INCREF(reinterpret_cast<PyObject*>(p->body));
    }
}

static void py_pass_ref_release(tc_pass* p) {
    if (p && p->body) {
        nb::gil_scoped_acquire gil;
        Py_DECREF(reinterpret_cast<PyObject*>(p->body));
    }
}

static void py_pass_ref_drop(tc_pass* p) {
    if (!p) {
        return;
    }
    tc_pass_free_external(p);
}

static const tc_pass_ref_vtable g_py_pass_ref_vtable = {
    py_pass_ref_retain,
    py_pass_ref_release,
    py_pass_ref_drop,
};

static bool g_py_callbacks_registered = false;

static void ensure_py_callbacks_registered() {
    if (!g_py_callbacks_registered) {
        tc_pass_set_external_callbacks(&g_py_pass_callbacks);
        g_py_callbacks_registered = true;
    }
}

void bind_tc_pass_runtime(nb::module_& m) {
    ensure_py_callbacks_registered();

    nb::enum_<tc_frame_graph_error>(m, "TcFrameGraphError")
        .value("OK", TC_FG_OK)
        .value("MULTI_WRITER", TC_FG_ERROR_MULTI_WRITER)
        .value("CYCLE", TC_FG_ERROR_CYCLE)
        .value("INVALID_INPLACE", TC_FG_ERROR_INVALID_INPLACE);

    nb::class_<TcPassRef>(m, "TcPassRef")
        .def(nb::init<>())
        .def("valid", &TcPassRef::valid)
        .def_prop_rw("pass_name", &TcPassRef::pass_name, &TcPassRef::set_pass_name)
        .def_prop_ro("type_name", &TcPassRef::type_name)
        .def_prop_rw("enabled", &TcPassRef::enabled, &TcPassRef::set_enabled)
        .def_prop_rw("passthrough", &TcPassRef::passthrough, &TcPassRef::set_passthrough)
        .def("is_inplace", &TcPassRef::is_inplace)
        .def_prop_ro("inplace", &TcPassRef::is_inplace)
        .def_prop_ro("reads", [](TcPassRef& self) {
            std::set<std::string> result;
            tc_pass* p = self.ptr();
            if (p) {
                const char* names[64];
                size_t count = tc_pass_get_reads(p, names, 64);
                for (size_t i = 0; i < count; ++i) {
                    if (names[i]) {
                        result.insert(names[i]);
                    }
                }
            }
            return result;
        })
        .def_prop_ro("writes", [](TcPassRef& self) {
            std::set<std::string> result;
            tc_pass* p = self.ptr();
            if (p) {
                const char* names[64];
                size_t count = tc_pass_get_writes(p, names, 64);
                for (size_t i = 0; i < count; ++i) {
                    if (names[i]) {
                        result.insert(names[i]);
                    }
                }
            }
            return result;
        })
        .def("get_inplace_aliases", [](TcPassRef& self) {
            std::vector<std::tuple<std::string, std::string>> result;
            tc_pass* p = self.ptr();
            if (p) {
                const char* pairs[32];
                size_t count = tc_pass_get_inplace_aliases(p, pairs, 32);
                for (size_t i = 0; i + 1 < count; i += 2) {
                    result.emplace_back(pairs[i] ? pairs[i] : "", pairs[i + 1] ? pairs[i + 1] : "");
                }
            }
            return result;
        })
        .def("get_internal_symbols", [](TcPassRef& self) {
            std::vector<std::string> result;
            tc_pass* p = self.ptr();
            if (p) {
                const char* symbols[64];
                size_t count = tc_pass_get_internal_symbols(p, symbols, 64);
                for (size_t i = 0; i < count; ++i) {
                    if (symbols[i]) {
                        result.push_back(symbols[i]);
                    }
                }
            }
            return result;
        })
        .def("get_internal_symbols_with_timing", [](TcPassRef& self) {
            std::vector<InternalSymbolTiming> result;
            tc_pass* p = self.ptr();
            if (p && p->kind == TC_NATIVE_PASS) {
                CxxFramePass* fp = CxxFramePass::from_tc(p);
                if (fp) {
                    return fp->get_internal_symbols_with_timing();
                }
            }
            return result;
        })
        .def("set_debug_internal_point", [](TcPassRef& self, const std::string& symbol) {
            tc_pass* p = self.ptr();
            if (!p) {
                return;
            }
            if (p->debug_internal_symbol) {
                free(p->debug_internal_symbol);
                p->debug_internal_symbol = nullptr;
            }
            if (!symbol.empty()) {
                p->debug_internal_symbol = strdup(symbol.c_str());
            }
        }, nb::arg("symbol"))
        .def("get_debug_internal_point", [](TcPassRef& self) -> std::string {
            tc_pass* p = self.ptr();
            if (p && p->debug_internal_symbol) {
                return std::string(p->debug_internal_symbol);
            }
            return "";
        })
        .def("clear_debug_internal_point", [](TcPassRef& self) {
            tc_pass* p = self.ptr();
            if (p && p->debug_internal_symbol) {
                free(p->debug_internal_symbol);
                p->debug_internal_symbol = nullptr;
            }
        })
        .def("set_debug_capture", [](TcPassRef& self, FrameGraphCapture* c) {
            tc_pass* p = self.ptr();
            if (p) {
                p->debug_capture = c;
            }
        }, nb::arg("capture"))
        .def("clear_debug_capture", [](TcPassRef& self) {
            tc_pass* p = self.ptr();
            if (p) {
                p->debug_capture = nullptr;
            }
        })
        .def("get_debug_capture", [](TcPassRef& self) -> FrameGraphCapture* {
            tc_pass* p = self.ptr();
            return p ? static_cast<FrameGraphCapture*>(p->debug_capture) : nullptr;
        }, nb::rv_policy::reference)
        .def("set_debugger_window", [](TcPassRef& self, nb::object window, nb::object depth_callback, nb::object error_callback) {
            nb::object py_obj = tc_pass_to_python(self.ptr());
            if (!py_obj.is_none() && nb::hasattr(py_obj, "set_debugger_window")) {
                py_obj.attr("set_debugger_window")(window, depth_callback, error_callback);
            }
        }, nb::arg("window") = nb::none(), nb::arg("depth_callback") = nb::none(), nb::arg("error_callback") = nb::none())
        .def("get_debugger_window", [](TcPassRef& self) -> nb::object {
            nb::object py_obj = tc_pass_to_python(self.ptr());
            if (!py_obj.is_none() && nb::hasattr(py_obj, "get_debugger_window")) {
                return py_obj.attr("get_debugger_window")();
            }
            return nb::none();
        })
        .def("to_python", [](TcPassRef& self) {
            return tc_pass_to_python(self.ptr());
        })
        .def_prop_ro("viewport_name", [](TcPassRef& self) {
            tc_pass* p = self.ptr();
            return p && p->viewport_name ? std::string(p->viewport_name) : std::string();
        })
        .def("serialize_data", [](TcPassRef& self) -> nb::object {
            tc_pass* p = self.ptr();
            if (!p) {
                return nb::none();
            }

            void* obj_ptr = nullptr;
            if (p->kind == TC_NATIVE_PASS) {
                obj_ptr = CxxFramePass::from_tc(p);
            } else {
                obj_ptr = p->body;
            }
            if (!obj_ptr) {
                return nb::none();
            }

            tc_value v = tc_inspect_serialize(obj_ptr, tc_pass_type_name(p));
            nb::object result = tc_value_to_py(&v);
            tc_value_free(&v);
            return result;
        })
        .def("serialize", [](TcPassRef& self) -> nb::object {
            tc_pass* p = self.ptr();
            if (!p) {
                return nb::none();
            }

            if (p->kind == TC_EXTERNAL_PASS && p->body && p->native_language == TC_LANGUAGE_PYTHON) {
                nb::object py_obj = nb::borrow<nb::object>(reinterpret_cast<PyObject*>(p->body));
                if (nb::hasattr(py_obj, "serialize")) {
                    nb::object result = py_obj.attr("serialize")();
                    if (!result.is_none()) {
                        return result;
                    }
                }
            }

            void* obj_ptr = nullptr;
            if (p->kind == TC_NATIVE_PASS) {
                obj_ptr = CxxFramePass::from_tc(p);
            } else {
                obj_ptr = p->body;
            }

            nb::object data = nb::dict();
            if (obj_ptr) {
                tc_value v = tc_inspect_serialize(obj_ptr, tc_pass_type_name(p));
                data = tc_value_to_py(&v);
                tc_value_free(&v);
            }

            nb::dict result;
            result["type"] = self.type_name();
            result["pass_name"] = self.pass_name();
            result["enabled"] = p->enabled;
            result["passthrough"] = p->passthrough;
            result["viewport_name"] = p->viewport_name ? std::string(p->viewport_name) : std::string();
            result["data"] = data;
            return result;
        })
        .def("deserialize_data", [](TcPassRef& self, nb::object data) {
            tc_pass* p = self.ptr();
            if (!p || data.is_none()) {
                return;
            }

            void* obj_ptr = nullptr;
            if (p->kind == TC_NATIVE_PASS) {
                obj_ptr = CxxFramePass::from_tc(p);
            } else {
                obj_ptr = p->body;
            }
            if (!obj_ptr) {
                return;
            }

            tc_value v = py_to_tc_value(data);
            tc_inspect_deserialize(obj_ptr, tc_pass_type_name(p), &v, nullptr);
            tc_value_free(&v);
        }, nb::arg("data"))
        .def("get_field", [](TcPassRef& self, const std::string& field_name) -> nb::object {
            tc_pass* p = self.ptr();
            if (!p) {
                return nb::none();
            }

            void* obj_ptr = nullptr;
            if (p->kind == TC_NATIVE_PASS) {
                obj_ptr = CxxFramePass::from_tc(p);
            } else {
                obj_ptr = p->body;
            }
            if (!obj_ptr) {
                return nb::none();
            }

            try {
                return tc::InspectRegistry_get(tc::InspectRegistry::instance(), obj_ptr, tc_pass_type_name(p), field_name);
            } catch (const std::exception& e) {
                tc_log(TC_LOG_ERROR, "[tc_pass] get_field(%s) failed: %s", field_name.c_str(), e.what());
                return nb::none();
            } catch (...) {
                tc_log(TC_LOG_ERROR, "[tc_pass] get_field(%s) failed with unknown exception", field_name.c_str());
                return nb::none();
            }
        }, nb::arg("field_name"))
        .def("set_field", [](TcPassRef& self, const std::string& field_name, nb::object value) {
            tc_pass* p = self.ptr();
            if (!p || value.is_none()) {
                return;
            }

            void* obj_ptr = nullptr;
            if (p->kind == TC_NATIVE_PASS) {
                obj_ptr = CxxFramePass::from_tc(p);
            } else {
                obj_ptr = p->body;
            }
            if (!obj_ptr) {
                return;
            }

            try {
                tc::InspectRegistry_set(tc::InspectRegistry::instance(), obj_ptr, tc_pass_type_name(p), field_name, value, nullptr);
            } catch (const std::exception& e) {
                tc_log(TC_LOG_ERROR, "[tc_pass] set_field(%s) failed: %s", field_name.c_str(), e.what());
            } catch (...) {
                tc_log(TC_LOG_ERROR, "[tc_pass] set_field(%s) failed with unknown exception", field_name.c_str());
            }
        }, nb::arg("field_name"), nb::arg("value"))
        .def("set_viewport_name", [](TcPassRef& self, const std::string& name) {
            tc_pass* p = self.ptr();
            if (!p) {
                return;
            }
            if (p->viewport_name) {
                free((void*) p->viewport_name);
            }
            p->viewport_name = name.empty() ? nullptr : strdup(name.c_str());
        }, nb::arg("name"));

    nb::class_<TcPass>(m, "TcPass")
        .def("__init__", [](TcPass* self, nb::object py_self, const std::string& type_name) {
            ensure_py_callbacks_registered();
            tc_pass* c = tc_pass_new_external(py_self.ptr(), type_name.c_str(), &g_py_pass_ref_vtable);
            new (self) TcPass(c);
        }, nb::arg("py_self"), nb::arg("type_name"))
        .def("ref", &TcPass::ref)
        .def_prop_rw("pass_name", &TcPass::pass_name, &TcPass::set_pass_name)
        .def_prop_ro("type_name", &TcPass::type_name)
        .def_prop_rw("enabled", &TcPass::enabled, &TcPass::set_enabled)
        .def_prop_rw("passthrough", &TcPass::passthrough, &TcPass::set_passthrough)
        .def("is_inplace", &TcPass::is_inplace);

    m.def("tc_pipeline_create", [](const std::string& name) -> std::tuple<uint32_t, uint32_t> {
        tc_pipeline_handle h = tc_pipeline_create(name.c_str());
        return std::make_tuple(h.index, h.generation);
    }, nb::arg("name") = "default");

    m.def("tc_pipeline_destroy", [](std::tuple<uint32_t, uint32_t> h) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        tc_pipeline_destroy(handle);
    });

    m.def("tc_pipeline_get_name", [](std::tuple<uint32_t, uint32_t> h) -> std::string {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        const char* n = tc_pipeline_get_name(handle);
        return n ? std::string(n) : std::string();
    });

    m.def("tc_pipeline_pass_count", [](std::tuple<uint32_t, uint32_t> h) -> size_t {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        return tc_pipeline_pass_count(handle);
    });

    m.def("tc_pipeline_pool_alive", [](std::tuple<uint32_t, uint32_t> h) -> bool {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        return tc_pipeline_pool_alive(handle);
    });

    m.def("tc_pipeline_add_pass", [](std::tuple<uint32_t, uint32_t> h, TcPassRef pass_ref) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass_ref.valid()) {
            tc_pipeline_add_pass(handle, pass_ref.ptr());
        }
    });

    m.def("tc_pipeline_add_pass", [](std::tuple<uint32_t, uint32_t> h, TcPass* pass) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass && pass->ptr()) {
            tc_pipeline_add_pass(handle, pass->ptr());
        }
    });

    m.def("tc_pipeline_remove_pass", [](std::tuple<uint32_t, uint32_t> h, TcPassRef pass_ref) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass_ref.valid()) {
            tc_pipeline_remove_pass(handle, pass_ref.ptr());
        }
    });

    m.def("tc_pipeline_remove_pass", [](std::tuple<uint32_t, uint32_t> h, TcPass* pass) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass && pass->ptr()) {
            tc_pipeline_remove_pass(handle, pass->ptr());
        }
    });

    m.def("tc_pipeline_remove_passes_by_name", [](std::tuple<uint32_t, uint32_t> h, const std::string& name) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        return tc_pipeline_remove_passes_by_name(handle, name.c_str());
    });

    m.def("tc_pipeline_insert_pass_before", [](std::tuple<uint32_t, uint32_t> h, TcPassRef pass_ref, TcPassRef before_ref) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass_ref.valid()) {
            tc_pipeline_insert_pass_before(handle, pass_ref.ptr(), before_ref.ptr());
        }
    });

    m.def("tc_pipeline_insert_pass_before", [](std::tuple<uint32_t, uint32_t> h, TcPass* pass, TcPassRef before_ref) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass && pass->ptr()) {
            tc_pipeline_insert_pass_before(handle, pass->ptr(), before_ref.ptr());
        }
    });

    m.def("tc_pipeline_get_pass", [](std::tuple<uint32_t, uint32_t> h, const std::string& name) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        return TcPassRef(tc_pipeline_get_pass(handle, name.c_str()));
    });

    m.def("tc_pipeline_get_pass_at", [](std::tuple<uint32_t, uint32_t> h, size_t index) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        return TcPassRef(tc_pipeline_get_pass_at(handle, index));
    });

    m.def("tc_pipeline_is_dirty", [](std::tuple<uint32_t, uint32_t> h) -> bool {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        return tc_pipeline_is_dirty(handle);
    });

    m.def("tc_pipeline_mark_dirty", [](std::tuple<uint32_t, uint32_t> h) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        tc_pipeline_mark_dirty(handle);
    });

    m.def("tc_pipeline_clear_dirty", [](std::tuple<uint32_t, uint32_t> h) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        tc_pipeline_clear_dirty(handle);
    });

    m.def("tc_frame_graph_build", [](std::tuple<uint32_t, uint32_t> h) -> intptr_t {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        return reinterpret_cast<intptr_t>(tc_frame_graph_build(handle));
    });

    m.def("tc_frame_graph_destroy", [](intptr_t fg_ptr) {
        tc_frame_graph_destroy(reinterpret_cast<tc_frame_graph*>(fg_ptr));
    });

    m.def("tc_frame_graph_get_error", [](intptr_t fg_ptr) {
        return tc_frame_graph_get_error(reinterpret_cast<tc_frame_graph*>(fg_ptr));
    });

    m.def("tc_frame_graph_get_error_message", [](intptr_t fg_ptr) {
        const char* msg = tc_frame_graph_get_error_message(reinterpret_cast<tc_frame_graph*>(fg_ptr));
        return msg ? std::string(msg) : std::string();
    });

    m.def("tc_frame_graph_schedule_count", [](intptr_t fg_ptr) {
        return tc_frame_graph_schedule_count(reinterpret_cast<tc_frame_graph*>(fg_ptr));
    });

    m.def("tc_frame_graph_schedule_at", [](intptr_t fg_ptr, size_t index) {
        return TcPassRef(tc_frame_graph_schedule_at(reinterpret_cast<tc_frame_graph*>(fg_ptr), index));
    });

    m.def("tc_frame_graph_get_schedule", [](intptr_t fg_ptr) {
        tc_frame_graph* fg = reinterpret_cast<tc_frame_graph*>(fg_ptr);
        nb::list result;
        size_t count = tc_frame_graph_schedule_count(fg);
        for (size_t i = 0; i < count; i++) {
            tc_pass* p = tc_frame_graph_schedule_at(fg, i);
            result.append(TcPassRef(p));
        }
        return result;
    });

    m.def("tc_frame_graph_canonical_resource", [](intptr_t fg_ptr, const std::string& name) {
        return std::string(tc_frame_graph_canonical_resource(reinterpret_cast<tc_frame_graph*>(fg_ptr), name.c_str()));
    });

    m.def("tc_frame_graph_dump", [](intptr_t fg_ptr) {
        tc_frame_graph_dump(reinterpret_cast<tc_frame_graph*>(fg_ptr));
    });

    m.def("tc_frame_graph_get_alias_groups", [](intptr_t fg_ptr) {
        tc_frame_graph* fg = reinterpret_cast<tc_frame_graph*>(fg_ptr);
        nb::dict result;

        const char* canonical_names[256];
        size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

        for (size_t i = 0; i < canon_count; i++) {
            const char* canon = canonical_names[i];
            const char* alias_names[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, alias_names, 64);

            nb::list aliases;
            for (size_t j = 0; j < alias_count; j++) {
                aliases.append(nb::str(alias_names[j]));
            }

            result[nb::str(canon)] = aliases;
        }

        return result;
    });

    m.def("tc_resources_allocate_dict", [](intptr_t fg_ptr, nb::dict specs_dict, nb::object target_fbo, int width, int height) -> nb::dict {
        tc_frame_graph* fg = reinterpret_cast<tc_frame_graph*>(fg_ptr);
        nb::dict result;

        const char* canonical_names[256];
        size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

        result["OUTPUT"] = target_fbo;
        result["DISPLAY"] = target_fbo;

        for (size_t i = 0; i < canon_count; i++) {
            const char* canon = canonical_names[i];

            const char* alias_names[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, alias_names, 64);

            bool is_display = (strcmp(canon, "DISPLAY") == 0 || strcmp(canon, "OUTPUT") == 0);
            if (is_display) {
                for (size_t j = 0; j < alias_count; j++) {
                    result[nb::str(alias_names[j])] = target_fbo;
                }
                continue;
            }

            nb::object spec = nb::none();
            if (specs_dict.contains(canon)) {
                spec = specs_dict[canon];
            } else {
                for (size_t j = 0; j < alias_count; j++) {
                    if (specs_dict.contains(alias_names[j])) {
                        spec = specs_dict[alias_names[j]];
                        break;
                    }
                }
            }

            std::string resource_type = "fbo";
            if (!spec.is_none() && nb::hasattr(spec, "resource_type")) {
                nb::object rt = spec.attr("resource_type");
                if (!rt.is_none()) {
                    resource_type = nb::cast<std::string>(rt);
                }
            }

            if (resource_type != "fbo") {
                for (size_t j = 0; j < alias_count; j++) {
                    result[nb::str(alias_names[j])] = nb::none();
                }
                continue;
            }

            nb::dict fbo_info;
            fbo_info["canonical"] = nb::str(canon);
            fbo_info["width"] = width;
            fbo_info["height"] = height;
            fbo_info["samples"] = 1;
            fbo_info["format"] = nb::str("");

            if (!spec.is_none()) {
                if (nb::hasattr(spec, "size") && !spec.attr("size").is_none()) {
                    nb::tuple size = nb::cast<nb::tuple>(spec.attr("size"));
                    fbo_info["width"] = size[0];
                    fbo_info["height"] = size[1];
                }
                if (nb::hasattr(spec, "samples")) {
                    fbo_info["samples"] = spec.attr("samples");
                }
                if (nb::hasattr(spec, "format") && !spec.attr("format").is_none()) {
                    fbo_info["format"] = spec.attr("format");
                }
            }

            for (size_t j = 0; j < alias_count; j++) {
                result[nb::str(alias_names[j])] = fbo_info;
            }
        }

        return result;
    });

    m.def("tc_pipeline_collect_specs", [](std::tuple<uint32_t, uint32_t> h) -> nb::dict {
        nb::dict result;
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};

        ResourceSpec specs[256];
        size_t count = tc_pipeline_collect_specs(handle, specs, 256);
        for (size_t i = 0; i < count; ++i) {
            const ResourceSpec& spec = specs[i];
            nb::dict spec_dict;
            spec_dict["resource"] = nb::str(spec.resource.c_str());
            spec_dict["resource_type"] = nb::str(spec.resource_type.c_str());
            if (spec.size) {
                spec_dict["size"] = nb::make_tuple(spec.size->first, spec.size->second);
            }
            spec_dict["samples"] = spec.samples;
            if (spec.clear_color) {
                const auto& cc = *spec.clear_color;
                spec_dict["clear_color"] = nb::make_tuple(cc[0], cc[1], cc[2], cc[3]);
            }
            if (spec.clear_depth) {
                spec_dict["clear_depth"] = *spec.clear_depth;
            }
            if (spec.format) {
                spec_dict["format"] = nb::str(spec.format->c_str());
            }
            result[nb::str(spec.resource.c_str())] = spec_dict;
        }

        return result;
    });

    m.def("tc_pipeline_registry_count", []() {
        return tc_pipeline_registry_count();
    });

    m.def("tc_pipeline_registry_get_all_info", []() {
        nb::list result;
        size_t count = 0;
        tc_pipeline_info* infos = tc_pipeline_registry_get_all_info(&count);
        if (infos) {
            for (size_t i = 0; i < count; i++) {
                nb::dict info;
                info["handle"] = nb::make_tuple(infos[i].handle.index, infos[i].handle.generation);
                info["name"] = infos[i].name ? nb::str(infos[i].name) : nb::none();
                info["pass_count"] = infos[i].pass_count;
                result.append(info);
            }
            free(infos);
        }
        return result;
    });

    m.def("tc_pass_registry_get_all_instance_info", []() {
        nb::list result;
        size_t count = 0;
        tc_pass_info* infos = tc_pass_registry_get_all_instance_info(&count);
        if (infos) {
            for (size_t i = 0; i < count; i++) {
                nb::dict info;
                info["pass_name"] = infos[i].pass_name ? nb::str(infos[i].pass_name) : nb::none();
                info["type_name"] = infos[i].type_name ? nb::str(infos[i].type_name) : nb::none();
                info["pipeline_handle"] = nb::make_tuple(infos[i].pipeline_handle.index, infos[i].pipeline_handle.generation);
                info["pipeline_name"] = infos[i].pipeline_name ? nb::str(infos[i].pipeline_name) : nb::none();
                info["enabled"] = infos[i].enabled;
                info["passthrough"] = infos[i].passthrough;
                info["is_inplace"] = infos[i].is_inplace;
                info["kind"] = infos[i].kind == TC_NATIVE_PASS ? "native" : "external";
                result.append(info);
            }
            free(infos);
        }
        return result;
    });

    m.def("tc_pass_registry_type_count", []() {
        return tc_pass_registry_type_count();
    });

    m.def("tc_pass_registry_get_all_types", []() {
        nb::list result;
        size_t count = tc_pass_registry_type_count();
        for (size_t i = 0; i < count; i++) {
            const char* type_name = tc_pass_registry_type_at(i);
            if (type_name) {
                nb::dict info;
                info["type_name"] = nb::str(type_name);
                info["language"] = tc_pass_registry_get_kind(type_name) == TC_NATIVE_PASS ? "C++" : "Python";
                result.append(info);
            }
        }
        return result;
    });

    m.def("tc_pass_registry_register_python", [](const std::string& type_name, nb::object cls) {
        auto cls_ptr = std::make_shared<nb::object>(std::move(cls));
        python_pass_classes()[type_name] = cls_ptr;
        char* stable_name = strdup(type_name.c_str());
        tc_pass_registry_register(type_name.c_str(), python_pass_factory, stable_name, TC_EXTERNAL_PASS);
    });

    m.def("tc_pass_registry_has", [](const std::string& type_name) {
        return tc_pass_registry_has(type_name.c_str());
    });
}

} // namespace termin
