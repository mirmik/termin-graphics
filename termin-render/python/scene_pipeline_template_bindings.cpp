// scene_pipeline_template_bindings.cpp - Python bindings for TcScenePipelineTemplate
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include "termin/render/render_pipeline.hpp"
#include "termin/render/scene_pipeline_template.hpp"

extern "C" {
#include "tc_value.h"
}

namespace nb = nanobind;

namespace termin {

static nb::object tc_value_to_python(const tc_value& v);
static tc_value python_to_tc_value(nb::handle obj);

static nb::object tc_value_to_python(const tc_value& v) {
    switch (v.type) {
        case TC_VALUE_NIL:
            return nb::none();
        case TC_VALUE_BOOL:
            return nb::bool_(v.data.b);
        case TC_VALUE_INT:
            return nb::int_(v.data.i);
        case TC_VALUE_FLOAT:
            return nb::float_(static_cast<double>(v.data.f));
        case TC_VALUE_DOUBLE:
            return nb::float_(v.data.d);
        case TC_VALUE_STRING:
            return nb::str(v.data.s ? v.data.s : "");
        case TC_VALUE_VEC3: {
            nb::list result;
            result.append(v.data.v3.x);
            result.append(v.data.v3.y);
            result.append(v.data.v3.z);
            return result;
        }
        case TC_VALUE_QUAT: {
            nb::list result;
            result.append(v.data.q.w);
            result.append(v.data.q.x);
            result.append(v.data.q.y);
            result.append(v.data.q.z);
            return result;
        }
        case TC_VALUE_LIST: {
            nb::list result;
            for (size_t i = 0; i < v.data.list.count; i++) {
                result.append(tc_value_to_python(v.data.list.items[i]));
            }
            return result;
        }
        case TC_VALUE_DICT: {
            nb::dict result;
            for (size_t i = 0; i < v.data.dict.count; i++) {
                const char* key = v.data.dict.entries[i].key;
                if (key && v.data.dict.entries[i].value) {
                    result[key] = tc_value_to_python(*v.data.dict.entries[i].value);
                }
            }
            return result;
        }
        default:
            return nb::none();
    }
}

static tc_value python_to_tc_value(nb::handle obj) {
    if (obj.is_none()) {
        return tc_value_nil();
    }
    if (nb::isinstance<nb::bool_>(obj)) {
        return tc_value_bool(nb::cast<bool>(obj));
    }
    if (nb::isinstance<nb::int_>(obj)) {
        return tc_value_int(nb::cast<int64_t>(obj));
    }
    if (nb::isinstance<nb::float_>(obj)) {
        return tc_value_double(nb::cast<double>(obj));
    }
    if (nb::isinstance<nb::str>(obj)) {
        std::string s = nb::cast<std::string>(obj);
        return tc_value_string(s.c_str());
    }
    if (nb::isinstance<nb::list>(obj) || nb::isinstance<nb::tuple>(obj)) {
        nb::sequence seq = nb::cast<nb::sequence>(obj);
        tc_value list = tc_value_list_new();
        for (size_t i = 0; i < nb::len(seq); i++) {
            tc_value item = python_to_tc_value(seq[i]);
            tc_value_list_push(&list, item);
        }
        return list;
    }
    if (nb::isinstance<nb::dict>(obj)) {
        nb::dict d = nb::cast<nb::dict>(obj);
        tc_value dict = tc_value_dict_new();
        for (auto [key, value] : d) {
            std::string key_str = nb::cast<std::string>(nb::str(key));
            tc_value val = python_to_tc_value(value);
            tc_value_dict_set(&dict, key_str.c_str(), val);
        }
        return dict;
    }

    try {
        std::string s = nb::cast<std::string>(nb::str(obj));
        return tc_value_string(s.c_str());
    } catch (...) {
        return tc_value_nil();
    }
}

void bind_scene_pipeline_template(nb::module_& m) {
    nb::class_<TcScenePipelineTemplate>(m, "TcScenePipelineTemplate")
        .def(nb::init<>())
        .def_static("declare", &TcScenePipelineTemplate::declare, nb::arg("uuid"), nb::arg("name"))
        .def_static("find_by_uuid", &TcScenePipelineTemplate::find_by_uuid, nb::arg("uuid"))
        .def_static("find_by_name", &TcScenePipelineTemplate::find_by_name, nb::arg("name"))
        .def_prop_ro("is_valid", &TcScenePipelineTemplate::is_valid)
        .def_prop_ro("is_loaded", &TcScenePipelineTemplate::is_loaded)
        .def_prop_ro("uuid", &TcScenePipelineTemplate::uuid)
        .def_prop_rw("name", &TcScenePipelineTemplate::name, &TcScenePipelineTemplate::set_name)
        .def("set_from_json", &TcScenePipelineTemplate::set_from_json, nb::arg("json"))
        .def("to_json", &TcScenePipelineTemplate::to_json)
        .def_prop_ro("graph_data", [](TcScenePipelineTemplate& self) -> nb::object {
            const tc_value* v = self.get_graph();
            if (!v || v->type == TC_VALUE_NIL) {
                return nb::none();
            }
            return tc_value_to_python(*v);
        })
        .def("set_graph_data", [](TcScenePipelineTemplate& self, nb::dict data) {
            tc_value v = python_to_tc_value(data);
            self.set_graph(v);
        }, nb::arg("data"))
        .def_prop_ro("target_viewports", &TcScenePipelineTemplate::target_viewports)
        .def("compile", &TcScenePipelineTemplate::compile, nb::rv_policy::take_ownership)
        .def("ensure_loaded", &TcScenePipelineTemplate::ensure_loaded)
        .def_prop_ro("_handle", [](TcScenePipelineTemplate& self) {
            auto h = self.handle();
            return std::make_tuple(h.index, h.generation);
        });
}

} // namespace termin
