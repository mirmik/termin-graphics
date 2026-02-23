// mesh_bindings.cpp - Mesh3 and TcMesh Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/optional.h>
#include <optional>
#include <unordered_map>
#include <mutex>

#include "tgfx/tgfx_mesh3.hpp"
#include "tgfx/tgfx_mesh_handle.hpp"

extern "C" {
#include <tcbase/tc_log.h>
}

namespace nb = nanobind;

using namespace termin;

namespace {

// Python callback storage for lazy loading
static std::mutex g_callback_mutex;
static std::unordered_map<std::string, nb::callable> g_python_callbacks;

void cleanup_mesh_callbacks() {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    g_python_callbacks.clear();
}

static bool python_load_callback_wrapper(tc_mesh* mesh, void* user_data) {
    (void)user_data;
    if (!mesh) return false;

    std::string uuid(mesh->header.uuid);

    nb::callable callback;
    {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        auto it = g_python_callbacks.find(uuid);
        if (it == g_python_callbacks.end()) {
            return false;
        }
        callback = it->second;
    }

    nb::gil_scoped_acquire gil;
    try {
        nb::object result = callback(nb::cast(mesh, nb::rv_policy::reference));
        return nb::cast<bool>(result);
    } catch (const std::exception& e) {
        tc_log_error("Python mesh load callback failed for '%s': %s", uuid.c_str(), e.what());
        return false;
    }
}

// Helper: create 2D numpy array of floats
template<typename T>
nb::object make_array_2d(const T* data, size_t rows, size_t cols) {
    T* buf = new T[rows * cols];
    for (size_t i = 0; i < rows * cols; ++i) {
        buf[i] = data[i];
    }
    nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<T*>(p); });
    size_t shape[2] = {rows, cols};
    return nb::cast(nb::ndarray<nb::numpy, T>(buf, 2, shape, owner));
}

// Helper: create 1D numpy array
template<typename T>
nb::object make_array_1d(const T* data, size_t size) {
    T* buf = new T[size];
    for (size_t i = 0; i < size; ++i) {
        buf[i] = data[i];
    }
    nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<T*>(p); });
    return nb::cast(nb::ndarray<nb::numpy, T>(buf, {size}, owner));
}

} // anonymous namespace

namespace tgfx_bindings {

void bind_mesh(nb::module_& m) {
    // =========================================================================
    // Mesh3 - Pure CPU mesh
    // =========================================================================
    nb::class_<Mesh3>(m, "Mesh3")
        .def(nb::init<>())
        .def("__init__", [](Mesh3* self,
                           nb::ndarray<float, nb::c_contig, nb::device::cpu> vertices,
                           nb::ndarray<uint32_t, nb::c_contig, nb::device::cpu> triangles,
                           nb::object uvs_obj,
                           nb::object normals_obj,
                           std::string name,
                           std::string uuid) {
            size_t num_verts = vertices.shape(0);
            float* v_ptr = vertices.data();

            std::vector<Vec3f> verts(num_verts);
            for (size_t i = 0; i < num_verts; ++i) {
                verts[i] = Vec3f(v_ptr[i * 3], v_ptr[i * 3 + 1], v_ptr[i * 3 + 2]);
            }

            size_t num_tris = triangles.shape(0);
            uint32_t* t_ptr = triangles.data();

            std::vector<uint32_t> tris(num_tris * 3);
            for (size_t i = 0; i < num_tris * 3; ++i) {
                tris[i] = t_ptr[i];
            }

            new (self) Mesh3(std::move(verts), std::move(tris), name, uuid);

            // Optional normals
            if (!normals_obj.is_none()) {
                auto n_arr = nb::cast<nb::ndarray<float, nb::c_contig, nb::device::cpu>>(normals_obj);
                float* n_ptr = n_arr.data();
                size_t n = n_arr.shape(0);
                self->normals.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    self->normals[i] = Vec3f(n_ptr[i * 3], n_ptr[i * 3 + 1], n_ptr[i * 3 + 2]);
                }
            }

            // Optional UVs
            if (!uvs_obj.is_none()) {
                auto uv_arr = nb::cast<nb::ndarray<float, nb::c_contig, nb::device::cpu>>(uvs_obj);
                float* uv_ptr = uv_arr.data();
                size_t n = uv_arr.shape(0);
                self->uvs.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    self->uvs[i] = Vec2f(uv_ptr[i * 2], uv_ptr[i * 2 + 1]);
                }
            }
        },
             nb::arg("vertices"),
             nb::arg("triangles"),
             nb::arg("uvs") = nb::none(),
             nb::arg("vertex_normals") = nb::none(),
             nb::arg("name") = "",
             nb::arg("uuid") = "")

        .def_prop_ro("vertices", [](const Mesh3& m) {
            size_t n = m.vertices.size();
            std::vector<float> flat(n * 3);
            for (size_t i = 0; i < n; ++i) {
                flat[i * 3] = m.vertices[i].x;
                flat[i * 3 + 1] = m.vertices[i].y;
                flat[i * 3 + 2] = m.vertices[i].z;
            }
            return make_array_2d(flat.data(), n, 3);
        })

        .def_prop_ro("triangles", [](const Mesh3& m) {
            size_t n = m.triangles.size() / 3;
            return make_array_2d(m.triangles.data(), n, 3);
        })

        .def_prop_ro("vertex_normals", [](const Mesh3& m) -> nb::object {
            if (m.normals.empty()) return nb::none();
            size_t n = m.normals.size();
            std::vector<float> flat(n * 3);
            for (size_t i = 0; i < n; ++i) {
                flat[i * 3] = m.normals[i].x;
                flat[i * 3 + 1] = m.normals[i].y;
                flat[i * 3 + 2] = m.normals[i].z;
            }
            return make_array_2d(flat.data(), n, 3);
        })

        .def_prop_ro("uvs", [](const Mesh3& m) -> nb::object {
            if (m.uvs.empty()) return nb::none();
            size_t n = m.uvs.size();
            std::vector<float> flat(n * 2);
            for (size_t i = 0; i < n; ++i) {
                flat[i * 2] = m.uvs[i].x;
                flat[i * 2 + 1] = m.uvs[i].y;
            }
            return make_array_2d(flat.data(), n, 2);
        })

        .def_prop_ro("tangents", [](const Mesh3& m) -> nb::object {
            if (m.tangents.empty()) return nb::none();
            size_t n = m.tangents.size();
            std::vector<float> flat(n * 4);
            for (size_t i = 0; i < n; ++i) {
                flat[i * 4] = m.tangents[i].x;
                flat[i * 4 + 1] = m.tangents[i].y;
                flat[i * 4 + 2] = m.tangents[i].z;
                flat[i * 4 + 3] = m.tangents[i].w;
            }
            return make_array_2d(flat.data(), n, 4);
        })

        .def_prop_rw("name",
            [](const Mesh3& m) { return m.name; },
            [](Mesh3& m, const std::string& n) { m.name = n; })

        .def_prop_rw("uuid",
            [](const Mesh3& m) { return m.uuid; },
            [](Mesh3& m, const std::string& u) { m.uuid = u; })

        .def_prop_ro("vertex_count", &Mesh3::vertex_count)
        .def_prop_ro("triangle_count", &Mesh3::triangle_count)

        .def("is_valid", &Mesh3::is_valid)
        .def("has_normals", &Mesh3::has_normals)
        .def("has_uvs", &Mesh3::has_uvs)
        .def("has_tangents", &Mesh3::has_tangents)
        .def("compute_normals", &Mesh3::compute_normals)
        .def("compute_tangents", &Mesh3::compute_tangents)

        .def("translate", [](Mesh3& m, float x, float y, float z) {
            m.translate(x, y, z);
        }, nb::arg("x"), nb::arg("y"), nb::arg("z"))

        .def("scale", [](Mesh3& m, float factor) {
            m.scale(factor);
        }, nb::arg("factor"))

        .def("copy", &Mesh3::copy, nb::arg("new_name") = "")

        .def("__repr__", [](const Mesh3& m) {
            return "<Mesh3 vertices=" + std::to_string(m.vertex_count()) +
                   " triangles=" + std::to_string(m.triangle_count()) +
                   " name=\"" + m.name + "\"" +
                   " uuid=\"" + m.uuid + "\">";
        });

    // =========================================================================
    // TcMesh - GPU-ready mesh
    // =========================================================================
    nb::enum_<tc_attrib_type>(m, "TcAttribType")
        .value("FLOAT32", TC_ATTRIB_FLOAT32)
        .value("INT32", TC_ATTRIB_INT32)
        .value("UINT32", TC_ATTRIB_UINT32)
        .value("INT16", TC_ATTRIB_INT16)
        .value("UINT16", TC_ATTRIB_UINT16)
        .value("INT8", TC_ATTRIB_INT8)
        .value("UINT8", TC_ATTRIB_UINT8);

    nb::enum_<tc_draw_mode>(m, "TcDrawMode")
        .value("TRIANGLES", TC_DRAW_TRIANGLES)
        .value("LINES", TC_DRAW_LINES);

    nb::class_<tc_vertex_layout>(m, "TcVertexLayout")
        .def("__init__", [](tc_vertex_layout* self) {
            new (self) tc_vertex_layout();
            tc_vertex_layout_init(self);
        })
        .def_ro("stride", &tc_vertex_layout::stride)
        .def_ro("attrib_count", &tc_vertex_layout::attrib_count)
        .def("add", [](tc_vertex_layout& self, const std::string& name, uint8_t size, tc_attrib_type type, uint8_t location) {
            return tc_vertex_layout_add(&self, name.c_str(), size, type, location);
        }, nb::arg("name"), nb::arg("size"), nb::arg("type"), nb::arg("location"))
        .def("find", [](const tc_vertex_layout& self, const std::string& name) -> nb::object {
            const tc_vertex_attrib* attr = tc_vertex_layout_find(&self, name.c_str());
            if (!attr) return nb::none();
            nb::dict d;
            d["name"] = std::string(attr->name);
            d["size"] = attr->size;
            d["type"] = static_cast<tc_attrib_type>(attr->type);
            d["location"] = attr->location;
            d["offset"] = attr->offset;
            return d;
        }, nb::arg("name"))
        .def_static("pos_normal_uv", []() { return tc_vertex_layout_pos_normal_uv(); })
        .def_static("pos_normal_uv_tangent", []() { return tc_vertex_layout_pos_normal_uv_tangent(); })
        .def_static("pos_normal_uv_color", []() { return tc_vertex_layout_pos_normal_uv_color(); })
        .def_static("skinned", []() { return tc_vertex_layout_skinned(); });

    // Raw tc_mesh struct binding
    nb::class_<tc_mesh>(m, "TcMeshData")
        .def_ro("vertex_count", &tc_mesh::vertex_count)
        .def_ro("index_count", &tc_mesh::index_count)
        .def_prop_ro("version", [](const tc_mesh& m) { return m.header.version; })
        .def_prop_ro("ref_count", [](const tc_mesh& m) { return m.header.ref_count; })
        .def_prop_ro("uuid", [](const tc_mesh& m) { return std::string(m.header.uuid); })
        .def_prop_ro("name", [](const tc_mesh& m) { return m.header.name ? std::string(m.header.name) : ""; })
        .def_prop_ro("stride", [](const tc_mesh& m) { return m.layout.stride; })
        .def_prop_ro("layout", [](const tc_mesh& m) { return m.layout; })
        .def("get_vertices_buffer", [](const tc_mesh& m) -> nb::object {
            if (!m.vertices || m.vertex_count == 0) return nb::none();
            size_t total_floats = (m.vertex_count * m.layout.stride) / sizeof(float);
            return make_array_1d((const float*)m.vertices, total_floats);
        })
        .def("get_indices_buffer", [](const tc_mesh& m) -> nb::object {
            if (!m.indices || m.index_count == 0) return nb::none();
            return make_array_1d(m.indices, m.index_count);
        });

    // TcMesh - GPU-ready mesh wrapper
    nb::class_<TcMesh>(m, "TcMesh")
        .def(nb::init<>())
        .def_prop_ro("is_valid", &TcMesh::is_valid)
        .def_prop_ro("uuid", &TcMesh::uuid)
        .def_prop_ro("name", &TcMesh::name)
        .def_prop_ro("version", &TcMesh::version)
        .def_prop_ro("vertex_count", &TcMesh::vertex_count)
        .def_prop_ro("index_count", &TcMesh::index_count)
        .def_prop_ro("triangle_count", &TcMesh::triangle_count)
        .def_prop_ro("stride", &TcMesh::stride)
        .def_prop_rw("draw_mode",
            &TcMesh::draw_mode,
            &TcMesh::set_draw_mode)
        .def_prop_ro("mesh", [](const TcMesh& h) -> nb::object {
            tc_mesh* m = h.get();
            if (!m) return nb::none();
            return nb::cast(m, nb::rv_policy::reference);
        })
        .def_prop_ro("vertices", [](const TcMesh& h) -> nb::object {
            tc_mesh* m = h.get();
            if (!m || !m->vertices || m->vertex_count == 0) return nb::none();
            const tc_vertex_attrib* pos = tc_vertex_layout_find(&m->layout, "position");
            if (!pos || pos->size != 3) return nb::none();
            size_t n = m->vertex_count;
            size_t stride = m->layout.stride;
            float* buf = new float[n * 3];
            const uint8_t* src = static_cast<const uint8_t*>(m->vertices);
            for (size_t i = 0; i < n; ++i) {
                const float* p = reinterpret_cast<const float*>(src + i * stride + pos->offset);
                buf[i * 3] = p[0];
                buf[i * 3 + 1] = p[1];
                buf[i * 3 + 2] = p[2];
            }
            nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<float*>(p); });
            size_t shape[2] = {n, 3};
            return nb::cast(nb::ndarray<nb::numpy, float>(buf, 2, shape, owner));
        })
        .def_prop_ro("triangles", [](const TcMesh& h) -> nb::object {
            tc_mesh* m = h.get();
            if (!m || !m->indices || m->index_count == 0) return nb::none();
            size_t n = m->index_count / 3;
            uint32_t* buf = new uint32_t[n * 3];
            for (size_t i = 0; i < n * 3; ++i) {
                buf[i] = m->indices[i];
            }
            nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<uint32_t*>(p); });
            size_t shape[2] = {n, 3};
            return nb::cast(nb::ndarray<nb::numpy, uint32_t>(buf, 2, shape, owner));
        })
        .def_prop_ro("vertex_normals", [](const TcMesh& h) -> nb::object {
            tc_mesh* m = h.get();
            if (!m || !m->vertices || m->vertex_count == 0) return nb::none();
            const tc_vertex_attrib* norm = tc_vertex_layout_find(&m->layout, "normal");
            if (!norm || norm->size != 3) return nb::none();
            size_t n = m->vertex_count;
            size_t stride = m->layout.stride;
            float* buf = new float[n * 3];
            const uint8_t* src = static_cast<const uint8_t*>(m->vertices);
            for (size_t i = 0; i < n; ++i) {
                const float* p = reinterpret_cast<const float*>(src + i * stride + norm->offset);
                buf[i * 3] = p[0];
                buf[i * 3 + 1] = p[1];
                buf[i * 3 + 2] = p[2];
            }
            nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<float*>(p); });
            size_t shape[2] = {n, 3};
            return nb::cast(nb::ndarray<nb::numpy, float>(buf, 2, shape, owner));
        })
        .def("bump_version", &TcMesh::bump_version)
        .def("set_from_mesh3", [](TcMesh& h, const Mesh3& mesh) {
            return h.set_from_mesh3(mesh);
        }, nb::arg("mesh"))
        .def("get_vertices_buffer", [](const TcMesh& h) -> nb::object {
            tc_mesh* m = h.get();
            if (!m || !m->vertices || m->vertex_count == 0) return nb::none();
            size_t total_floats = (m->vertex_count * m->layout.stride) / sizeof(float);
            return make_array_1d((const float*)m->vertices, total_floats);
        })
        .def("get_indices_buffer", [](const TcMesh& h) -> nb::object {
            tc_mesh* m = h.get();
            if (!m || !m->indices || m->index_count == 0) return nb::none();
            return make_array_1d(m->indices, m->index_count);
        })
        .def_static("from_mesh3", [](const Mesh3& mesh, std::string name, std::string uuid) {
            return TcMesh::from_mesh3(mesh, name, uuid);
        }, nb::arg("mesh"), nb::arg("name") = "", nb::arg("uuid") = "")
        .def_static("from_interleaved", [](
                nb::ndarray<nb::c_contig, nb::device::cpu> vertices,
                size_t vertex_count,
                nb::ndarray<uint32_t, nb::c_contig, nb::device::cpu> indices,
                const tc_vertex_layout& layout,
                std::string name,
                std::string uuid_hint,
                tc_draw_mode draw_mode) {
            return TcMesh::from_interleaved(
                vertices.data(), vertex_count,
                indices.data(), indices.size(),
                layout, name, uuid_hint, draw_mode);
        }, nb::arg("vertices"), nb::arg("vertex_count"), nb::arg("indices"),
           nb::arg("layout"), nb::arg("name") = "", nb::arg("uuid") = "",
           nb::arg("draw_mode") = TC_DRAW_TRIANGLES)
        .def_static("from_uuid", &TcMesh::from_uuid, nb::arg("uuid"))
        .def_static("get_or_create", &TcMesh::get_or_create, nb::arg("uuid"))
        .def_static("from_name", [](const std::string& name) {
            tc_mesh_handle h = tc_mesh_find_by_name(name.c_str());
            if (tc_mesh_handle_is_invalid(h)) return TcMesh();
            return TcMesh(h);
        }, nb::arg("name"))
        .def_static("list_all_names", []() {
            std::vector<std::string> names;
            tc_mesh_foreach([](tc_mesh_handle, tc_mesh* mesh, void* user_data) -> bool {
                auto* vec = static_cast<std::vector<std::string>*>(user_data);
                if (mesh && mesh->header.name) {
                    vec->push_back(mesh->header.name);
                }
                return true;
            }, &names);
            return names;
        })
        .def("upload_gpu", [](TcMesh& h) {
            tc_mesh* m = h.get();
            if (m) tc_mesh_upload_gpu(m);
        }, "Upload mesh to GPU (if not already uploaded)")
        .def("draw_gpu", [](TcMesh& h) {
            tc_mesh* m = h.get();
            if (m) {
                tc_mesh_upload_gpu(m);
                tc_mesh_draw_gpu(m);
            }
        }, "Upload (if needed) and draw mesh")
        .def("__repr__", [](const TcMesh& h) {
            tc_mesh* m = h.get();
            if (!m) return std::string("<TcMesh invalid>");
            return "<TcMesh vertices=" + std::to_string(m->vertex_count) +
                   " triangles=" + std::to_string(m->index_count / 3) +
                   " uuid=" + std::string(m->header.uuid) + ">";
        });

    // Alias for backward compatibility
    m.attr("TcMeshHandle") = m.attr("TcMesh");

    // =========================================================================
    // Module-level functions
    // =========================================================================
    m.def("tc_mesh_compute_uuid", [](nb::ndarray<float, nb::c_contig, nb::device::cpu> vertices,
                                      nb::ndarray<uint32_t, nb::c_contig, nb::device::cpu> indices) {
        char uuid[40];
        tc_mesh_compute_uuid(vertices.data(), vertices.size() * sizeof(float),
                            indices.data(), indices.size(), uuid);
        return std::string(uuid);
    }, nb::arg("vertices"), nb::arg("indices"),
       "Compute UUID from vertex and index data (hash-based)");

    m.def("tc_mesh_get", [](const std::string& uuid) -> std::optional<TcMesh> {
        tc_mesh_handle h = tc_mesh_find(uuid.c_str());
        if (tc_mesh_handle_is_invalid(h)) return std::nullopt;
        return TcMesh(h);
    }, nb::arg("uuid"),
       "Get existing mesh by UUID (returns None if not found)");

    m.def("tc_mesh_get_or_create", [](const std::string& uuid) {
        tc_mesh_handle h = tc_mesh_get_or_create(uuid.c_str());
        return TcMesh(h);
    }, nb::arg("uuid"),
       "Get existing mesh or create new one");

    m.def("tc_mesh_set_data", [](TcMesh& handle,
                                  nb::ndarray<float, nb::c_contig, nb::device::cpu> vertices, size_t vertex_count,
                                  const tc_vertex_layout& layout,
                                  nb::ndarray<uint32_t, nb::c_contig, nb::device::cpu> indices,
                                  const std::string& name) {
        tc_mesh* m = handle.get();
        if (!m) return false;
        return tc_mesh_set_data(m,
                               vertices.data(), vertex_count, &layout,
                               indices.data(), indices.size(),
                               name.empty() ? nullptr : name.c_str());
    }, nb::arg("handle"), nb::arg("vertices"),
       nb::arg("vertex_count"), nb::arg("layout"), nb::arg("indices"),
       nb::arg("name") = "",
       "Set mesh vertex and index data");

    m.def("tc_mesh_contains", [](const std::string& uuid) {
        return tc_mesh_contains(uuid.c_str());
    }, nb::arg("uuid"), "Check if mesh exists in registry");

    m.def("tc_mesh_count", []() {
        return tc_mesh_count();
    }, "Get number of meshes in registry");

    m.def("tc_mesh_get_all_info", []() {
        nb::list result;
        size_t count = 0;
        tc_mesh_info* infos = tc_mesh_get_all_info(&count);
        if (infos) {
            for (size_t i = 0; i < count; ++i) {
                nb::dict d;
                d["uuid"] = std::string(infos[i].uuid);
                d["name"] = infos[i].name ? std::string(infos[i].name) : "";
                d["ref_count"] = infos[i].ref_count;
                d["version"] = infos[i].version;
                d["vertex_count"] = infos[i].vertex_count;
                d["index_count"] = infos[i].index_count;
                d["stride"] = infos[i].stride;
                d["memory_bytes"] = infos[i].memory_bytes;
                d["is_loaded"] = (bool)infos[i].is_loaded;
                d["has_load_callback"] = (bool)infos[i].has_load_callback;
                result.append(d);
            }
            free(infos);
        }
        return result;
    }, "Get info for all meshes in registry");

    // Lazy loading API
    m.def("tc_mesh_declare", [](const std::string& uuid, const std::string& name) {
        tc_mesh_handle h = tc_mesh_declare(uuid.c_str(), name.empty() ? nullptr : name.c_str());
        return TcMesh(h);
    }, nb::arg("uuid"), nb::arg("name") = "",
       "Declare a mesh that will be loaded lazily");

    m.def("tc_mesh_is_loaded", [](TcMesh& handle) {
        return tc_mesh_is_loaded(handle.handle);
    }, nb::arg("handle"), "Check if mesh data is loaded");

    m.def("tc_mesh_ensure_loaded", [](TcMesh& handle) {
        return tc_mesh_ensure_loaded(handle.handle);
    }, nb::arg("handle"), "Ensure mesh is loaded (triggers callback if needed)");

    m.def("tc_mesh_set_load_callback", [](TcMesh& handle, nb::callable callback) {
        tc_mesh* mesh = handle.get();
        if (!mesh) {
            throw std::runtime_error("Invalid mesh handle");
        }

        std::string uuid(mesh->header.uuid);

        {
            std::lock_guard<std::mutex> lock(g_callback_mutex);
            g_python_callbacks[uuid] = callback;
        }

        tc_mesh_set_load_callback(handle.handle, python_load_callback_wrapper, nullptr);
    }, nb::arg("handle"), nb::arg("callback"),
       "Set Python callback for lazy loading");

    m.def("tc_mesh_clear_load_callback", [](TcMesh& handle) {
        tc_mesh* mesh = handle.get();
        if (!mesh) return;

        std::string uuid(mesh->header.uuid);

        {
            std::lock_guard<std::mutex> lock(g_callback_mutex);
            g_python_callbacks.erase(uuid);
        }

        tc_mesh_set_load_callback(handle.handle, nullptr, nullptr);
    }, nb::arg("handle"), "Clear load callback for mesh");

    // Register cleanup with Python's atexit module
    nb::module_ atexit = nb::module_::import_("atexit");
    atexit.attr("register")(nb::cpp_function(&cleanup_mesh_callbacks));
}

} // namespace tgfx_bindings
