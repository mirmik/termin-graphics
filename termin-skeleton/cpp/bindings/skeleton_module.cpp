#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/array.h>
#include <nanobind/ndarray.h>
#include <iostream>
#include <unordered_map>
#include <mutex>

#include "termin/skeleton/skeleton_instance.hpp"
#include "termin/skeleton/tc_skeleton_handle.hpp"
#include "termin/inspect/tc_kind.hpp"
#include <tcbase/tc_log.hpp>

namespace nb = nanobind;

// ============================================================================
// Python callback storage for lazy loading
// ============================================================================

static std::mutex g_skeleton_callback_mutex;
static std::unordered_map<std::string, nb::callable> g_skeleton_python_callbacks;

// Cleanup function to clear callbacks before Python shutdown
void cleanup_skeleton_callbacks() {
    std::lock_guard<std::mutex> lock(g_skeleton_callback_mutex);
    g_skeleton_python_callbacks.clear();
}

// C callback wrapper that calls the stored Python callable
static bool skeleton_python_load_callback_wrapper(tc_skeleton* skeleton, void* user_data) {
    (void)user_data;
    if (!skeleton) return false;

    std::string uuid(skeleton->header.uuid);

    nb::callable callback;
    {
        std::lock_guard<std::mutex> lock(g_skeleton_callback_mutex);
        auto it = g_skeleton_python_callbacks.find(uuid);
        if (it == g_skeleton_python_callbacks.end()) {
            return false;
        }
        callback = it->second;
    }

    nb::gil_scoped_acquire gil;
    try {
        nb::object result = callback(nb::cast(skeleton, nb::rv_policy::reference));
        return nb::cast<bool>(result);
    } catch (const std::exception& e) {
        tc::Log::error("Python skeleton load callback failed for '%s': %s", uuid.c_str(), e.what());
        return false;
    }
}

namespace {

// Helper: numpy/sequence (3,) -> std::array<double, 3>
std::array<double, 3> numpy_to_vec3(nb::object obj) {
    try {
        auto arr = nb::cast<nb::ndarray<double, nb::c_contig, nb::device::cpu>>(obj);
        double* ptr = arr.data();
        return {ptr[0], ptr[1], ptr[2]};
    } catch (...) {
        tc::Log::debug("[numpy_to_vec3] ndarray cast failed, falling back to sequence");
    }
    nb::sequence seq = nb::cast<nb::sequence>(obj);
    return {nb::cast<double>(seq[0]), nb::cast<double>(seq[1]), nb::cast<double>(seq[2])};
}

// Helper: numpy/sequence (4,) -> std::array<double, 4>
std::array<double, 4> numpy_to_vec4(nb::object obj) {
    try {
        auto arr = nb::cast<nb::ndarray<double, nb::c_contig, nb::device::cpu>>(obj);
        double* ptr = arr.data();
        return {ptr[0], ptr[1], ptr[2], ptr[3]};
    } catch (...) {
        tc::Log::debug("[numpy_to_vec4] ndarray cast failed, falling back to sequence");
    }
    nb::sequence seq = nb::cast<nb::sequence>(obj);
    return {nb::cast<double>(seq[0]), nb::cast<double>(seq[1]),
            nb::cast<double>(seq[2]), nb::cast<double>(seq[3])};
}

void bind_tc_bone(nb::module_& m) {
    nb::class_<tc_bone>(m, "TcBone")
        .def_prop_rw("name",
            [](const tc_bone& b) { return std::string(b.name); },
            [](tc_bone& b, const std::string& name) {
                size_t len = std::min(name.size(), (size_t)(TC_BONE_NAME_MAX - 1));
                std::memcpy(b.name, name.c_str(), len);
                b.name[len] = '\0';
            })
        .def_rw("index", &tc_bone::index)
        .def_rw("parent_index", &tc_bone::parent_index)
        .def_prop_rw("inverse_bind_matrix",
            [](const tc_bone& b) {
                nb::list result;
                for (int i = 0; i < 16; ++i) {
                    result.append(b.inverse_bind_matrix[i]);
                }
                return result;
            },
            [](tc_bone& b, nb::sequence seq) {
                for (int i = 0; i < 16 && i < nb::len(seq); ++i) {
                    b.inverse_bind_matrix[i] = nb::cast<double>(seq[i]);
                }
            })
        .def_prop_rw("bind_translation",
            [](const tc_bone& b) {
                return nb::make_tuple(b.bind_translation[0], b.bind_translation[1], b.bind_translation[2]);
            },
            [](tc_bone& b, nb::sequence seq) {
                if (nb::len(seq) >= 3) {
                    b.bind_translation[0] = nb::cast<double>(seq[0]);
                    b.bind_translation[1] = nb::cast<double>(seq[1]);
                    b.bind_translation[2] = nb::cast<double>(seq[2]);
                }
            })
        .def_prop_rw("bind_rotation",
            [](const tc_bone& b) {
                return nb::make_tuple(b.bind_rotation[0], b.bind_rotation[1], b.bind_rotation[2], b.bind_rotation[3]);
            },
            [](tc_bone& b, nb::sequence seq) {
                if (nb::len(seq) >= 4) {
                    b.bind_rotation[0] = nb::cast<double>(seq[0]);
                    b.bind_rotation[1] = nb::cast<double>(seq[1]);
                    b.bind_rotation[2] = nb::cast<double>(seq[2]);
                    b.bind_rotation[3] = nb::cast<double>(seq[3]);
                }
            })
        .def_prop_rw("bind_scale",
            [](const tc_bone& b) {
                return nb::make_tuple(b.bind_scale[0], b.bind_scale[1], b.bind_scale[2]);
            },
            [](tc_bone& b, nb::sequence seq) {
                if (nb::len(seq) >= 3) {
                    b.bind_scale[0] = nb::cast<double>(seq[0]);
                    b.bind_scale[1] = nb::cast<double>(seq[1]);
                    b.bind_scale[2] = nb::cast<double>(seq[2]);
                }
            });
}

void bind_tc_skeleton_struct(nb::module_& m) {
    // Bind tc_skeleton C struct for low-level access
    nb::class_<tc_skeleton>(m, "tc_skeleton_struct")
        .def_prop_rw("is_loaded",
            [](const tc_skeleton& s) { return s.header.is_loaded != 0; },
            [](tc_skeleton& s, bool loaded) { s.header.is_loaded = loaded ? 1 : 0; })
        .def_prop_ro("uuid", [](const tc_skeleton& s) { return std::string(s.header.uuid); })
        .def_prop_ro("name", [](const tc_skeleton& s) { return s.header.name ? std::string(s.header.name) : ""; })
        .def_prop_ro("bone_count", [](const tc_skeleton& s) { return s.bone_count; })
        .def_prop_ro("root_count", [](const tc_skeleton& s) { return s.root_count; });
}

void bind_tc_skeleton(nb::module_& m) {
    nb::class_<termin::TcSkeleton>(m, "TcSkeleton")
        .def(nb::init<>())
        .def_prop_ro("is_valid", &termin::TcSkeleton::is_valid)
        .def_prop_ro("uuid", [](const termin::TcSkeleton& s) {
            return std::string(s.uuid());
        })
        .def_prop_ro("name", [](const termin::TcSkeleton& s) {
            return std::string(s.name());
        })
        .def_prop_ro("version", &termin::TcSkeleton::version)
        .def_prop_ro("bone_count", &termin::TcSkeleton::bone_count)
        .def_prop_ro("root_count", &termin::TcSkeleton::root_count)
        .def("find_bone", &termin::TcSkeleton::find_bone, nb::arg("bone_name"))
        .def("bump_version", &termin::TcSkeleton::bump_version)
        .def("ensure_loaded", &termin::TcSkeleton::ensure_loaded)
        .def("rebuild_roots", &termin::TcSkeleton::rebuild_roots)
        .def("get", &termin::TcSkeleton::get, nb::rv_policy::reference)
        .def("get_bone", &termin::TcSkeleton::get_bone, nb::arg("index"), nb::rv_policy::reference)
        .def("alloc_bones", &termin::TcSkeleton::alloc_bones, nb::arg("count"), nb::rv_policy::reference)
        .def_static("from_uuid", &termin::TcSkeleton::from_uuid, nb::arg("uuid"))
        .def_static("get_or_create", &termin::TcSkeleton::get_or_create, nb::arg("uuid"))
        .def_static("create", &termin::TcSkeleton::create,
            nb::arg("name") = "", nb::arg("uuid_hint") = "")
        .def("serialize", [](const termin::TcSkeleton& s) {
            nb::dict d;
            if (!s.is_valid()) {
                d["type"] = "none";
                return d;
            }
            d["uuid"] = std::string(s.uuid());
            d["name"] = std::string(s.name());
            d["type"] = "uuid";
            return d;
        })
        .def_static("deserialize", [](nb::dict data) {
            if (!data.contains("uuid")) {
                return termin::TcSkeleton();
            }
            std::string uuid = nb::cast<std::string>(data["uuid"]);
            return termin::TcSkeleton::from_uuid(uuid);
        }, nb::arg("data"))
        .def("__repr__", [](const termin::TcSkeleton& s) {
            if (!s.is_valid()) return std::string("<TcSkeleton invalid>");
            return "<TcSkeleton '" + std::string(s.name()) + "' bones=" +
                   std::to_string(s.bone_count()) + ">";
        });
}

void register_tc_skeleton_kind() {
    // C++ handler for tc_skeleton kind
    tc::KindRegistry::instance().register_cpp("tc_skeleton",
        // serialize: std::any(TcSkeleton) → tc_value
        [](const std::any& value) -> tc_value {
            const termin::TcSkeleton& s = std::any_cast<const termin::TcSkeleton&>(value);
            tc_value result = tc_value_dict_new();
            if (s.is_valid()) {
                tc_value_dict_set(&result, "uuid", tc_value_string(s.uuid()));
                tc_value_dict_set(&result, "name", tc_value_string(s.name()));
            }
            return result;
        },
        // deserialize: tc_value, scene → std::any(TcSkeleton)
        [](const tc_value* v, void*) -> std::any {
            if (!v || v->type != TC_VALUE_DICT) return termin::TcSkeleton();
            tc_value* uuid_val = tc_value_dict_get(const_cast<tc_value*>(v), "uuid");
            if (!uuid_val || uuid_val->type != TC_VALUE_STRING || !uuid_val->data.s) {
                return termin::TcSkeleton();
            }
            std::string uuid = uuid_val->data.s;
            termin::TcSkeleton skel = termin::TcSkeleton::from_uuid(uuid);
            if (!skel.is_valid()) {
                tc_value* name_val = tc_value_dict_get(const_cast<tc_value*>(v), "name");
                std::string name = (name_val && name_val->type == TC_VALUE_STRING && name_val->data.s)
                    ? name_val->data.s : "";
                tc::Log::warn("tc_skeleton deserialize: skeleton not found, uuid=%s name=%s", uuid.c_str(), name.c_str());
            } else {
                skel.ensure_loaded();
            }
            return skel;
        }
    );

    // Python handler for tc_skeleton kind
    tc::KindRegistry::instance().register_python(
        "tc_skeleton",
        // serialize
        nb::cpp_function([](nb::object obj) -> nb::object {
            termin::TcSkeleton skel = nb::cast<termin::TcSkeleton>(obj);
            nb::dict d;
            if (skel.is_valid()) {
                d["uuid"] = nb::str(skel.uuid());
                d["name"] = nb::str(skel.name());
            }
            return d;
        }),
        // deserialize
        nb::cpp_function([](nb::object data) -> nb::object {
            if (!nb::isinstance<nb::dict>(data)) {
                return nb::cast(termin::TcSkeleton());
            }
            nb::dict d = nb::cast<nb::dict>(data);
            if (!d.contains("uuid")) {
                return nb::cast(termin::TcSkeleton());
            }
            std::string uuid = nb::cast<std::string>(d["uuid"]);
            return nb::cast(termin::TcSkeleton::from_uuid(uuid));
        })
    );
}

void bind_skeleton_instance(nb::module_& m) {
    nb::class_<termin::SkeletonInstance>(m, "SkeletonInstance")
        .def(nb::init<>())
        .def_prop_rw("skeleton",
            [](const termin::SkeletonInstance& si) { return si._skeleton; },
            [](termin::SkeletonInstance& si, tc_skeleton* skel) { si.set_skeleton(skel); },
            nb::rv_policy::reference)
        .def_prop_rw("bone_entities",
            [](const termin::SkeletonInstance& si) {
                nb::list result;
                for (const termin::Entity& e : si.bone_entities()) {
                    if (e.valid()) {
                        result.append(nb::cast(e));
                    } else {
                        result.append(nb::none());
                    }
                }
                return result;
            },
            [](termin::SkeletonInstance& si, nb::list entities) {
                std::vector<termin::Entity> vec;
                for (auto item : entities) {
                    if (!item.is_none()) {
                        vec.push_back(nb::cast<termin::Entity>(item));
                    }
                }
                si.set_bone_entities(std::move(vec));
            })
        .def_prop_rw("skeleton_root",
            [](const termin::SkeletonInstance& si) -> nb::object {
                termin::Entity root = si.skeleton_root();
                if (root.valid()) return nb::cast(root);
                return nb::none();
            },
            [](termin::SkeletonInstance& si, nb::object root_obj) {
                if (root_obj.is_none()) {
                    si.set_skeleton_root(termin::Entity());
                } else {
                    si.set_skeleton_root(nb::cast<termin::Entity>(root_obj));
                }
            })
        .def("get_bone_entity", [](const termin::SkeletonInstance& si, int index) -> nb::object {
            termin::Entity e = si.get_bone_entity(index);
            if (e.valid()) return nb::cast(e);
            return nb::none();
        }, nb::arg("bone_index"))
        .def("get_bone_entity_by_name", [](const termin::SkeletonInstance& si, const std::string& name) -> nb::object {
            termin::Entity e = si.get_bone_entity_by_name(name);
            if (e.valid()) return nb::cast(e);
            return nb::none();
        }, nb::arg("bone_name"))
        .def("set_bone_transform", [](termin::SkeletonInstance& si,
                int bone_index,
                nb::object translation,
                nb::object rotation,
                nb::object scale) {
            std::array<double, 3> t_arr;
            std::array<double, 4> r_arr;
            std::array<double, 3> s_arr;
            const double* t_ptr = nullptr;
            const double* r_ptr = nullptr;
            const double* s_ptr = nullptr;

            if (!translation.is_none()) {
                t_arr = numpy_to_vec3(translation);
                t_ptr = t_arr.data();
            }
            if (!rotation.is_none()) {
                r_arr = numpy_to_vec4(rotation);
                r_ptr = r_arr.data();
            }
            if (!scale.is_none()) {
                if (nb::isinstance<nb::float_>(scale) || nb::isinstance<nb::int_>(scale)) {
                    double s = nb::cast<double>(scale);
                    s_arr = {s, s, s};
                } else {
                    s_arr = numpy_to_vec3(scale);
                }
                s_ptr = s_arr.data();
            }
            si.set_bone_transform(bone_index, t_ptr, r_ptr, s_ptr);
        }, nb::arg("bone_index"),
           nb::arg("translation") = nb::none(),
           nb::arg("rotation") = nb::none(),
           nb::arg("scale") = nb::none())
        .def("set_bone_transform_by_name", [](termin::SkeletonInstance& si,
                const std::string& bone_name,
                nb::object translation,
                nb::object rotation,
                nb::object scale) {
            std::array<double, 3> t_arr;
            std::array<double, 4> r_arr;
            std::array<double, 3> s_arr;
            const double* t_ptr = nullptr;
            const double* r_ptr = nullptr;
            const double* s_ptr = nullptr;

            if (!translation.is_none()) {
                t_arr = numpy_to_vec3(translation);
                t_ptr = t_arr.data();
            }
            if (!rotation.is_none()) {
                r_arr = numpy_to_vec4(rotation);
                r_ptr = r_arr.data();
            }
            if (!scale.is_none()) {
                if (nb::isinstance<nb::float_>(scale) || nb::isinstance<nb::int_>(scale)) {
                    double s = nb::cast<double>(scale);
                    s_arr = {s, s, s};
                } else {
                    s_arr = numpy_to_vec3(scale);
                }
                s_ptr = s_arr.data();
            }
            si.set_bone_transform_by_name(bone_name, t_ptr, r_ptr, s_ptr);
        }, nb::arg("bone_name"),
           nb::arg("translation") = nb::none(),
           nb::arg("rotation") = nb::none(),
           nb::arg("scale") = nb::none())
        .def("update", &termin::SkeletonInstance::update)
        .def("get_bone_matrices", [](termin::SkeletonInstance& si) {
            si.update();
            int n = si.bone_count();
            float* buf = new float[n * 16];
            for (int i = 0; i < n; ++i) {
                const termin::Mat44& m = si.get_bone_matrix(i);
                // Convert column-major Mat44 to row-major numpy array
                for (int row = 0; row < 4; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        buf[i * 16 + row * 4 + col] = static_cast<float>(m(col, row));
                    }
                }
            }
            nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<float*>(p); });
            size_t shape[3] = {static_cast<size_t>(n), 4, 4};
            return nb::ndarray<nb::numpy, float>(buf, 3, shape, owner);
        })
        .def("bone_count", &termin::SkeletonInstance::bone_count)
        .def("get_bone_world_matrix", [](const termin::SkeletonInstance& si, int bone_index) {
            termin::Mat44 m = si.get_bone_world_matrix(bone_index);
            float* buf = new float[16];
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    buf[row * 4 + col] = static_cast<float>(m(col, row));
                }
            }
            nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<float*>(p); });
            size_t shape[2] = {4, 4};
            return nb::ndarray<nb::numpy, float>(buf, 2, shape, owner);
        }, nb::arg("bone_index"))
        .def("__repr__", [](const termin::SkeletonInstance& si) {
            bool has_entities = !si.bone_entities().empty();
            return "<SkeletonInstance bones=" + std::to_string(si.bone_count()) +
                   " has_entities=" + (has_entities ? "True" : "False") + ">";
        });
}

} // anonymous namespace

NB_MODULE(_skeleton_native, m) {
    m.doc() = "Native C++ skeleton module (TcSkeleton, SkeletonInstance)";

    // Bind types
    bind_tc_bone(m);
    bind_tc_skeleton_struct(m);
    bind_tc_skeleton(m);
    bind_skeleton_instance(m);

    // Register tc_skeleton kind handler for InspectRegistry
    register_tc_skeleton_kind();

    // Lazy loading API
    m.def("tc_skeleton_declare", [](const std::string& uuid, const std::string& name) {
        tc_skeleton_handle h = tc_skeleton_declare(uuid.c_str(), name.empty() ? nullptr : name.c_str());
        return termin::TcSkeleton(h);
    }, nb::arg("uuid"), nb::arg("name") = "",
       "Declare a skeleton that will be loaded lazily");

    m.def("tc_skeleton_is_loaded", [](termin::TcSkeleton& handle) {
        return tc_skeleton_is_loaded(handle.handle);
    }, nb::arg("handle"), "Check if skeleton data is loaded");

    m.def("tc_skeleton_set_load_callback", [](termin::TcSkeleton& handle, nb::callable callback) {
        tc_skeleton* skeleton = handle.get();
        if (!skeleton) {
            throw std::runtime_error("Invalid skeleton handle");
        }

        std::string uuid(skeleton->header.uuid);

        {
            std::lock_guard<std::mutex> lock(g_skeleton_callback_mutex);
            g_skeleton_python_callbacks[uuid] = callback;
        }

        tc_skeleton_set_load_callback(handle.handle, skeleton_python_load_callback_wrapper, nullptr);
    }, nb::arg("handle"), nb::arg("callback"),
       "Set Python callback for lazy loading");

    m.def("tc_skeleton_clear_load_callback", [](termin::TcSkeleton& handle) {
        tc_skeleton* skeleton = handle.get();
        if (!skeleton) return;

        std::string uuid(skeleton->header.uuid);

        {
            std::lock_guard<std::mutex> lock(g_skeleton_callback_mutex);
            g_skeleton_python_callbacks.erase(uuid);
        }

        tc_skeleton_set_load_callback(handle.handle, nullptr, nullptr);
    }, nb::arg("handle"), "Clear load callback for skeleton");

    // Register cleanup with Python's atexit module
    nb::module_ atexit = nb::module_::import_("atexit");
    atexit.attr("register")(nb::cpp_function(&cleanup_skeleton_callbacks));
}
