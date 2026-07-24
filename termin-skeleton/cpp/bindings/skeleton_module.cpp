#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/optional.h>
#include <nanobind/ndarray.h>
#include <iostream>

#include "termin/geom/quat.hpp"
#include "termin/geom/vec3.hpp"
#include "termin/skeleton/skeleton_instance.hpp"
#include "termin/skeleton/tc_skeleton_handle.hpp"
#include "termin/inspect/tc_kind.hpp"
#include <tcbase/tc_log.hpp>

namespace nb = nanobind;

namespace {

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
    static bool registered = false;
    if (registered) {
        return;
    }

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

    nb::module_ skeleton_module = nb::module_::import_("termin.skeleton._skeleton_native");
    tc::KindRegistry::instance().register_type(skeleton_module.attr("TcSkeleton"), "tc_skeleton");

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

    registered = true;
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
                std::optional<termin::Vec3> translation,
                std::optional<termin::Quat> rotation,
                std::optional<termin::Vec3> scale) {
            const double* t_ptr = translation ? &translation->x : nullptr;
            const double* r_ptr = rotation ? &rotation->x : nullptr;
            const double* s_ptr = scale ? &scale->x : nullptr;
            si.set_bone_transform(bone_index, t_ptr, r_ptr, s_ptr);
        }, nb::arg("bone_index"),
           nb::arg("translation") = nb::none(),
           nb::arg("rotation") = nb::none(),
           nb::arg("scale") = nb::none())
        .def("set_bone_transform_by_name", [](termin::SkeletonInstance& si,
                const std::string& bone_name,
                std::optional<termin::Vec3> translation,
                std::optional<termin::Quat> rotation,
                std::optional<termin::Vec3> scale) {
            const double* t_ptr = translation ? &translation->x : nullptr;
            const double* r_ptr = rotation ? &rotation->x : nullptr;
            const double* s_ptr = scale ? &scale->x : nullptr;
            si.set_bone_transform_by_name(bone_name, t_ptr, r_ptr, s_ptr);
        }, nb::arg("bone_name"),
           nb::arg("translation") = nb::none(),
           nb::arg("rotation") = nb::none(),
           nb::arg("scale") = nb::none())
        .def("update", [](termin::SkeletonInstance& si) {
            si.update();
        })
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

    m.def("register_tc_skeleton_kind", &register_tc_skeleton_kind,
        "Register tc_skeleton kind handlers explicitly.");

    // Lazy loading API
    m.def("tc_skeleton_declare", [](const std::string& uuid, const std::string& name) {
        tc_skeleton_handle h = tc_skeleton_declare(uuid.c_str(), name.empty() ? nullptr : name.c_str());
        return termin::TcSkeleton(h);
    }, nb::arg("uuid"), nb::arg("name") = "",
       "Declare a skeleton that will be loaded lazily");

    m.def("tc_skeleton_is_loaded", [](termin::TcSkeleton& handle) {
        return tc_skeleton_is_loaded(handle.handle);
    }, nb::arg("handle"), "Check if skeleton data is loaded");

    m.def("tc_skeleton_get_all_info", []() {
        nb::list result;
        size_t count = 0;
        tc_skeleton_info* infos = tc_skeleton_get_all_info(&count);
        for (size_t i = 0; i < count; ++i) {
            nb::dict info;
            info["handle"] = nb::make_tuple(infos[i].handle.index, infos[i].handle.generation);
            info["uuid"] = std::string(infos[i].uuid);
            info["name"] = infos[i].name ? std::string(infos[i].name) : "";
            info["ref_count"] = infos[i].ref_count;
            info["version"] = infos[i].version;
            info["bone_count"] = infos[i].bone_count;
            info["is_loaded"] = infos[i].is_loaded != 0;
            result.append(info);
        }
        free(infos);
        return result;
    });

}
