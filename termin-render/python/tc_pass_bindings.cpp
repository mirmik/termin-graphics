#include <nanobind/nanobind.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/pair.h>
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
#include "tc_inspect_cpp.hpp"
#include "termin/bindings/tc_value_helpers.hpp"
#include "termin/render/execute_context.hpp"
#include "termin/render/frame_graph_debugger_core.hpp"
#include "termin/render/frame_pass.hpp"
#include "termin/render/resource_spec.hpp"
#include "termin/render/tc_pass.hpp"
#include "termin/render/unknown_pass.hpp"
#include "unknown_pass_serialization.hpp"
#include <tcbase/tc_string.h>

namespace nb = nanobind;

namespace termin {

using PassStringCollector = size_t (*)(tc_pass*, const char**, size_t);

static std::vector<const char*> collect_pass_strings(
    tc_pass* pass,
    PassStringCollector collect,
    size_t values_per_item = 1
) {
    size_t count = collect(pass, nullptr, 0);
    std::vector<const char*> values;
    while (count > 0) {
        values.resize(count * values_per_item);
        size_t actual = collect(pass, values.data(), count);
        if (actual <= count) {
            values.resize(actual * values_per_item);
            return values;
        }
        count = actual;
    }
    return values;
}

static std::vector<const char*> collect_frame_graph_canonical_names(tc_frame_graph* fg) {
    size_t count = tc_frame_graph_get_canonical_resources(fg, nullptr, 0);
    std::vector<const char*> values(count);
    count = tc_frame_graph_get_canonical_resources(fg, values.data(), values.size());
    values.resize(count);
    return values;
}

static std::vector<const char*> collect_frame_graph_alias_names(
    tc_frame_graph* fg,
    const char* canonical
) {
    size_t count = tc_frame_graph_get_alias_group(fg, canonical, nullptr, 0);
    std::vector<const char*> values(count);
    count = tc_frame_graph_get_alias_group(fg, canonical, values.data(), values.size());
    values.resize(count);
    return values;
}

static std::unordered_map<std::string, std::shared_ptr<nb::object>>& python_pass_classes() {
    static std::unordered_map<std::string, std::shared_ptr<nb::object>> classes;
    return classes;
}

static void py_owned_pass_deleter(tc_pass* p) {
    if (!p || !p->body) return;
    PyObject* body = reinterpret_cast<PyObject*>(p->body);
    nb::gil_scoped_acquire gil;
    Py_DECREF(body);
}

static bool adopt_pass_from_python_api(
    tc_pipeline_handle pipeline,
    tc_pass* pass,
    tc_pass* before = nullptr
) {
    if (!pass) return false;
    if (pass->native_language == TC_LANGUAGE_PYTHON && pass->body) {
        Py_INCREF(reinterpret_cast<PyObject*>(pass->body));
        const bool adopted = before
            ? tc_pipeline_adopt_pass_before(
                pipeline, pass, &py_owned_pass_deleter, before)
            : tc_pipeline_adopt_pass(
                pipeline, pass, &py_owned_pass_deleter);
        if (!adopted) {
            Py_DECREF(reinterpret_cast<PyObject*>(pass->body));
        }
        return adopted;
    }
    return before
        ? tc_pipeline_adopt_pass_before(pipeline, pass, pass->deleter, before)
        : tc_pipeline_adopt_pass(pipeline, pass, pass->deleter);
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

        nb::object tc_pass_ref_obj = py_obj.attr("_tc_pass");
        if (nb::isinstance<TcPassRef>(tc_pass_ref_obj)) {
            TcPassRef ref = nb::cast<TcPassRef>(tc_pass_ref_obj);
            tc_pass* p = ref.ptr();
            if (p) {
                Py_INCREF(py_obj.ptr());
                p->deleter = &py_owned_pass_deleter;
                p->bindings[TC_LANGUAGE_PYTHON] = py_obj.ptr();
                return p;
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

static bool lookup_py_method(nb::handle obj, const char* method_name, nb::object& out_method) {
    if (!nb::hasattr(obj, method_name)) {
        return false;
    }
    out_method = obj.attr(method_name);
    return true;
}

static size_t export_cached_string_list(nb::handle cached, const char** out, size_t max) {
    size_t count = 0;
    for (nb::handle item : cached) {
        if (out && count < max) {
            out[count] = PyUnicode_AsUTF8(item.ptr());
        }
        count++;
    }
    return count;
}

static size_t cache_string_iterable(
    nb::handle py_pass,
    nb::handle iterable,
    const char* cache_attr,
    const char** out,
    size_t max
) {
    nb::list cached;
    for (nb::handle item : iterable) {
        cached.append(nb::cast<nb::str>(item));
    }

    py_pass.attr(cache_attr) = cached;
    return export_cached_string_list(cached, out, max);
}

static size_t cache_string_pairs(
    nb::handle py_pass,
    nb::handle iterable,
    const char* cache_attr,
    const char** out,
    size_t max_pairs
) {
    nb::list cached;
    size_t pair_count = 0;
    for (nb::handle item : iterable) {
        nb::tuple pair = nb::cast<nb::tuple>(item);
        cached.append(nb::cast<nb::str>(pair[0]));
        cached.append(nb::cast<nb::str>(pair[1]));
        pair_count++;
    }

    py_pass.attr(cache_attr) = cached;
    export_cached_string_list(cached, out, out ? max_pairs * 2 : 0);
    return pair_count;
}

static size_t py_pass_string_callback(
    void* wrapper,
    const char* method_name,
    const char* cache_attr,
    const char* log_name,
    const char** out,
    size_t max
) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        nb::object method;
        if (!lookup_py_method(py_pass, method_name, method)) {
            return 0;
        }

        nb::object values = method();
        return cache_string_iterable(py_pass, values, cache_attr, out, max);
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python %s failed: %s", log_name, e.what());
        return 0;
    }
}

static void py_pass_void_callback(void* wrapper, const char* method_name, const char* log_name) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        nb::object method;
        if (!lookup_py_method(py_pass, method_name, method)) {
            return;
        }
        method();
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python %s failed: %s", log_name, e.what());
    }
}

static void apply_resource_spec_attrs(nb::handle py_spec, ResourceSpec& out_spec) {
    nb::object value;

    out_spec = ResourceSpec();
    out_spec.resource = nb::cast<std::string>(py_spec.attr("resource"));

    value = py_spec.attr("resource_type");
    if (!value.is_none()) {
        out_spec.resource_type = nb::cast<std::string>(value);
    } else {
        out_spec.resource_type = "fbo";
    }

    value = py_spec.attr("size");
    if (!value.is_none()) {
        nb::tuple size = nb::cast<nb::tuple>(value);
        out_spec.size = std::make_pair(nb::cast<int>(size[0]), nb::cast<int>(size[1]));
    }

    out_spec.samples = nb::cast<int>(py_spec.attr("samples"));

    value = py_spec.attr("clear_color");
    if (!value.is_none()) {
        nb::tuple color = nb::cast<nb::tuple>(value);
        out_spec.clear_color = std::array<double, 4>{
            nb::cast<double>(color[0]), nb::cast<double>(color[1]),
            nb::cast<double>(color[2]), nb::cast<double>(color[3])
        };
    }

    value = py_spec.attr("clear_depth");
    if (!value.is_none()) {
        out_spec.clear_depth = nb::cast<float>(value);
    }

    value = py_spec.attr("format");
    if (!value.is_none()) {
        out_spec.format = nb::cast<std::string>(value);
    }
}

static void py_pass_execute(void* wrapper, void* ctx) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        nb::object method;
        if (!lookup_py_method(py_pass, "execute", method)) {
            return;
        }

        ExecuteContext* exec_ctx = static_cast<ExecuteContext*>(ctx);
        method(nb::cast(exec_ctx, nb::rv_policy::reference));
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python execute failed: %s", e.what());
    }
}

static size_t py_pass_get_reads(void* wrapper, const char** out, size_t max) {
    return py_pass_string_callback(wrapper, "compute_reads", "_cached_tc_reads", "get_reads", out, max);
}

static size_t py_pass_get_writes(void* wrapper, const char** out, size_t max) {
    return py_pass_string_callback(wrapper, "compute_writes", "_cached_tc_writes", "get_writes", out, max);
}

static size_t py_pass_get_inplace_aliases(void* wrapper, const char** out, size_t max) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        nb::object method;
        if (!lookup_py_method(py_pass, "get_inplace_aliases", method)) {
            return 0;
        }

        nb::object aliases = method();
        return cache_string_pairs(py_pass, aliases, "_cached_tc_aliases", out, max);
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python get_inplace_aliases failed: %s", e.what());
        return 0;
    }
}

static size_t py_pass_get_resource_specs(void* wrapper, void* out, size_t max) {
    nb::gil_scoped_acquire gil;
    try {
        nb::object py_pass = nb::borrow<nb::object>(static_cast<PyObject*>(wrapper));
        nb::object method;
        if (!lookup_py_method(py_pass, "get_resource_specs", method)) {
            return 0;
        }

        nb::object specs = method();
        ResourceSpec* out_specs = static_cast<ResourceSpec*>(out);
        size_t count = 0;

        for (auto item : specs) {
            if (count >= max) {
                break;
            }

            apply_resource_spec_attrs(nb::borrow<nb::object>(item), out_specs[count]);
            count++;
        }

        return count;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Python get_resource_specs failed: %s", e.what());
        return 0;
    }
}

static size_t py_pass_get_internal_symbols(void* wrapper, const char** out, size_t max) {
    return py_pass_string_callback(wrapper, "get_internal_symbols", "_cached_tc_symbols", "get_internal_symbols", out, max);
}

static void py_pass_destroy(void* wrapper) {
    py_pass_void_callback(wrapper, "destroy", "destroy");
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
        .def_prop_ro("is_placeholder", [](TcPassRef& self) {
            return self.ptr() && std::string(tc_pass_type_name(self.ptr())) == "UnknownPass";
        })
        .def_prop_ro("original_type", [](TcPassRef& self) -> nb::object {
            if (!self.ptr() || std::string(tc_pass_type_name(self.ptr())) != "UnknownPass") {
                return nb::none();
            }
            auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(self.ptr()));
            return unknown ? nb::cast(unknown->original_type) : nb::none();
        })
        .def_prop_rw("enabled", &TcPassRef::enabled, &TcPassRef::set_enabled)
        .def_prop_rw("passthrough", &TcPassRef::passthrough, &TcPassRef::set_passthrough)
        .def("is_inplace", &TcPassRef::is_inplace)
        .def_prop_ro("inplace", &TcPassRef::is_inplace)
        .def_prop_ro("reads", [](TcPassRef& self) {
            std::set<std::string> result;
            tc_pass* p = self.ptr();
            if (p) {
                std::vector<const char*> names = collect_pass_strings(p, tc_pass_get_reads);
                for (size_t i = 0; i < names.size(); ++i) {
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
                std::vector<const char*> names = collect_pass_strings(p, tc_pass_get_writes);
                for (size_t i = 0; i < names.size(); ++i) {
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
                std::vector<const char*> pairs = collect_pass_strings(
                    p, tc_pass_get_inplace_aliases, 2);
                size_t count = pairs.size() / 2;
                for (size_t i = 0; i < count; i++) {
                    result.emplace_back(
                        pairs[i * 2] ? pairs[i * 2] : "",
                        pairs[i * 2 + 1] ? pairs[i * 2 + 1] : ""
                    );
                }
            }
            return result;
        })
        .def("get_internal_symbols", [](TcPassRef& self) {
            std::vector<std::string> result;
            tc_pass* p = self.ptr();
            if (p) {
                std::vector<const char*> symbols = collect_pass_strings(
                    p,
                    tc_pass_get_internal_symbols
                );
                result.reserve(symbols.size());
                for (const char* symbol : symbols) {
                    if (symbol) {
                        result.emplace_back(symbol);
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
            tc::init_cpp_inspect_vtable();
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
            tc::init_cpp_inspect_vtable();
            tc_pass* p = self.ptr();
            if (!p) {
                return nb::none();
            }

            if (std::string(tc_pass_type_name(p)) == "UnknownPass") {
                return serialize_unknown_pass_envelope(p);
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
            tc::init_cpp_inspect_vtable();
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
            tc::init_cpp_inspect_vtable();
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
            tc::init_cpp_inspect_vtable();
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
            tc_pass* c = tc_pass_new_external(py_self.ptr(), type_name.c_str());
            if (c) {
                c->native_language = TC_LANGUAGE_PYTHON;
            }
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
            if (!adopt_pass_from_python_api(handle, pass_ref.ptr())) {
                throw std::runtime_error("failed to adopt pass into pipeline");
            }
        }
    });

    m.def("tc_pipeline_add_pass", [](std::tuple<uint32_t, uint32_t> h, TcPass* pass) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass && pass->ptr()) {
            if (!adopt_pass_from_python_api(handle, pass->ptr())) {
                throw std::runtime_error("failed to adopt pass into pipeline");
            }
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
            if (!adopt_pass_from_python_api(handle, pass_ref.ptr(), before_ref.ptr())) {
                throw std::runtime_error("failed to adopt pass into pipeline");
            }
        }
    });

    m.def("tc_pipeline_insert_pass_before", [](std::tuple<uint32_t, uint32_t> h, TcPass* pass, TcPassRef before_ref) {
        tc_pipeline_handle handle = {std::get<0>(h), std::get<1>(h)};
        if (pass && pass->ptr()) {
            if (!adopt_pass_from_python_api(handle, pass->ptr(), before_ref.ptr())) {
                throw std::runtime_error("failed to adopt pass into pipeline");
            }
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

        std::vector<const char*> canonical_names = collect_frame_graph_canonical_names(fg);
        size_t canon_count = canonical_names.size();

        for (size_t i = 0; i < canon_count; i++) {
            const char* canon = canonical_names[i];
            std::vector<const char*> alias_names = collect_frame_graph_alias_names(fg, canon);
            size_t alias_count = alias_names.size();

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

        std::vector<const char*> canonical_names = collect_frame_graph_canonical_names(fg);
        size_t canon_count = canonical_names.size();

        result["OUTPUT"] = target_fbo;
        result["DISPLAY"] = target_fbo;

        for (size_t i = 0; i < canon_count; i++) {
            const char* canon = canonical_names[i];

            std::vector<const char*> alias_names = collect_frame_graph_alias_names(fg, canon);
            size_t alias_count = alias_names.size();

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
            if (!spec.is_none()) {
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
                nb::object size_obj = spec.attr("size");
                if (!size_obj.is_none()) {
                    nb::tuple size = nb::cast<nb::tuple>(size_obj);
                    fbo_info["width"] = size[0];
                    fbo_info["height"] = size[1];
                }
                fbo_info["samples"] = spec.attr("samples");
                nb::object format_obj = spec.attr("format");
                if (!format_obj.is_none()) {
                    fbo_info["format"] = format_obj;
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
                if (infos[i].ptr && std::string(tc_pass_type_name(infos[i].ptr)) == "UnknownPass") {
                    auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(infos[i].ptr));
                    if (unknown) info["original_type"] = unknown->original_type;
                }
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

    m.def("tc_pass_registry_register_python", [](
        const std::string& type_name,
        nb::object cls,
        const std::string& owner,
        nb::object parent,
        nb::dict fields,
        nb::dict metadata
    ) {
        if (owner.empty()) {
            tc_log(TC_LOG_ERROR, "[PythonFramePass] refusing ownerless type '%s'", type_name.c_str());
            return false;
        }
        const char* parent_name = nullptr;
        std::string parent_storage;
        if (!parent.is_none()) {
            parent_storage = nb::cast<std::string>(parent);
            parent_name = parent_storage.c_str();
        }
        const char* stable_name = tc_intern_string(type_name.c_str());
        if (!stable_name) {
            tc_log(TC_LOG_ERROR, "[PythonFramePass] failed to intern type '%s'", type_name.c_str());
            return false;
        }
        const char* existing_owner = tc_runtime_type_registry_get_owner(type_name.c_str());
        const bool allow_same_owner_replacement =
            existing_owner && owner == existing_owner;
        if (tc_pass_registry_has(type_name.c_str()) && !allow_same_owner_replacement) {
            tc_log(
                TC_LOG_ERROR,
                "[PythonFramePass] refusing replacement of type '%s' owned by '%s'",
                type_name.c_str(),
                existing_owner ? existing_owner : "<none>"
            );
            return false;
        }
        auto descriptor = FramePassTypeDescriptorBuilder(
            type_name.c_str(),
            owner.c_str(),
            parent_name,
            python_pass_factory,
            const_cast<char*>(stable_name),
            TC_EXTERNAL_PASS,
            allow_same_owner_replacement);
        auto inspect = tc::build_python_inspect_facet(type_name, std::move(fields));
        tc_value metadata_value = tc::nb_to_tc_value(std::move(metadata));
        const bool metadata_ok = inspect.set_metadata(&metadata_value);
        tc_value_free(&metadata_value);
        if (!metadata_ok) return false;
        descriptor.set_inspect(std::move(inspect));
        if (!descriptor.commit()) return false;
        python_pass_classes()[type_name] =
            std::make_shared<nb::object>(std::move(cls));
        return true;
    },
    nb::arg("type_name"),
    nb::arg("cls"),
    nb::arg("owner"),
    nb::arg("parent") = nb::none(),
    nb::arg("fields") = nb::dict(),
    nb::arg("metadata") = nb::dict());

    m.def("tc_pass_registry_unregister_python", [](const std::string& type_name) {
        if (tc_pass_registry_has(type_name.c_str()) &&
            tc_pass_registry_get_kind(type_name.c_str()) == TC_EXTERNAL_PASS) {
            tc_pass_registry_unregister(type_name.c_str());
        }
        python_pass_classes().erase(type_name);
    });

    m.def("tc_pass_registry_clear_python", []() {
        std::vector<std::string> type_names;
        type_names.reserve(python_pass_classes().size());
        for (const auto& item : python_pass_classes()) {
            type_names.push_back(item.first);
        }

        for (const auto& type_name : type_names) {
            if (tc_pass_registry_has(type_name.c_str()) &&
                tc_pass_registry_get_kind(type_name.c_str()) == TC_EXTERNAL_PASS) {
                tc_pass_registry_unregister(type_name.c_str());
            }
        }
        python_pass_classes().clear();
    });

    m.def("tc_pass_registry_has", [](const std::string& type_name) {
        return tc_pass_registry_has(type_name.c_str());
    });

    m.def("tc_pass_registry_is_native", [](const std::string& type_name) {
        if (!tc_pass_registry_has(type_name.c_str())) {
            return false;
        }
        return tc_pass_registry_get_kind(type_name.c_str()) == TC_NATIVE_PASS;
    });
}

} // namespace termin
