// texture_bindings.cpp - TcTexture Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/ndarray.h>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "tgfx/tgfx_texture_handle.hpp"

extern "C" {
#include <tcbase/tc_log.h>
}

namespace nb = nanobind;

using namespace termin;

namespace {

static std::mutex g_texture_callback_mutex;
static std::unordered_map<std::string, nb::callable> g_texture_python_callbacks;

void cleanup_texture_callbacks() {
    std::lock_guard<std::mutex> lock(g_texture_callback_mutex);
    g_texture_python_callbacks.clear();
}

static bool python_texture_load_callback_wrapper(void* resource, void* user_data) {
    (void)user_data;
    auto* tex = static_cast<tc_texture*>(resource);
    if (!tex) return false;

    std::string uuid(tex->header.uuid);

    nb::callable callback;
    {
        std::lock_guard<std::mutex> lock(g_texture_callback_mutex);
        auto it = g_texture_python_callbacks.find(uuid);
        if (it == g_texture_python_callbacks.end()) {
            return false;
        }
        callback = it->second;
    }

    nb::gil_scoped_acquire gil;
    try {
        tc_texture_handle h = tc_texture_find(uuid.c_str());
        nb::object result = callback(nb::cast(TcTexture(h)));
        return nb::cast<bool>(result);
    } catch (const std::exception& e) {
        tc_log_error("Python texture load callback failed for '%s': %s", uuid.c_str(), e.what());
        return false;
    }
}

} // anonymous namespace

namespace tgfx_bindings {

void bind_texture(nb::module_& m) {
    nb::class_<TcTexture>(m, "TcTexture")
        .def(nb::init<>())

        // Properties (read-only)
        .def_prop_ro("is_valid", &TcTexture::is_valid)
        .def_prop_ro("uuid", &TcTexture::uuid)
        .def_prop_ro("name", &TcTexture::name)
        .def_prop_ro("version", &TcTexture::version)
        .def_prop_ro("width", &TcTexture::width)
        .def_prop_ro("height", &TcTexture::height)
        .def_prop_ro("channels", &TcTexture::channels)
        .def_prop_ro("flip_x", &TcTexture::flip_x)
        .def_prop_ro("flip_y", &TcTexture::flip_y)
        .def_prop_ro("transpose", &TcTexture::transpose)
        .def_prop_ro("source_path", &TcTexture::source_path)
        .def_prop_ro("data_size", &TcTexture::data_size)

        // Data as numpy array (read-only, returns copy)
        .def_prop_ro("data", [](const TcTexture& self) -> nb::object {
            if (!self.is_valid() || !self.data()) {
                return nb::none();
            }
            size_t size = self.data_size();
            uint8_t* buf = new uint8_t[size];
            std::memcpy(buf, self.data(), size);

            size_t shape[3] = {
                static_cast<size_t>(self.height()),
                static_cast<size_t>(self.width()),
                static_cast<size_t>(self.channels())
            };

            nb::capsule owner(buf, [](void* p) noexcept {
                delete[] static_cast<uint8_t*>(p);
            });

            auto arr = nb::ndarray<nb::numpy, uint8_t, nb::shape<-1, -1, -1>>(
                buf, 3, shape, owner
            );
            return nb::cast(arr);
        })

        // Methods
        .def("bump_version", &TcTexture::bump_version)

        // Sync GPU-first texture to CPU
        .def("sync_to_cpu", &TcTexture::sync_to_cpu,
            "Sync GPU-first texture data to CPU. No-op for CPU-first textures. "
            "After a successful call, .data returns pixel content.")

        // GPU methods
        .def("bind_gpu", &TcTexture::bind_gpu, nb::arg("unit") = 0,
            "Bind texture to GPU texture unit")
        .def("upload_gpu", &TcTexture::upload_gpu,
            "Upload texture to GPU if needed")
        .def("delete_gpu", &TcTexture::delete_gpu,
            "Delete GPU resources")
        .def("needs_upload", &TcTexture::needs_upload,
            "Check if texture needs GPU upload")
        .def_prop_ro("gpu_id", &TcTexture::gpu_id,
            "OpenGL texture ID (0 if not uploaded)")
        .def("set_mipmap", &TcTexture::set_mipmap, nb::arg("enable"),
            "Enable/disable mipmap generation on upload")
        .def("set_clamp", &TcTexture::set_clamp, nb::arg("enable"),
            "Enable/disable clamp wrapping on upload")

        .def("set_transforms", &TcTexture::set_transforms,
            nb::arg("flip_x"), nb::arg("flip_y"), nb::arg("transpose"))

        .def("get_upload_data", [](const TcTexture& self) {
            auto [data, w, h] = self.get_upload_data();
            if (data.empty()) {
                return nb::make_tuple(nb::none(), nb::make_tuple(0, 0));
            }

            size_t size = data.size();
            uint8_t* buf = new uint8_t[size];
            std::memcpy(buf, data.data(), size);

            size_t shape[3] = {
                static_cast<size_t>(h),
                static_cast<size_t>(w),
                static_cast<size_t>(self.channels())
            };

            nb::capsule owner(buf, [](void* p) noexcept {
                delete[] static_cast<uint8_t*>(p);
            });

            auto arr = nb::ndarray<nb::numpy, uint8_t, nb::shape<-1, -1, -1>>(
                buf, 3, shape, owner
            );

            return nb::make_tuple(arr, nb::make_tuple(w, h));
        })

        // Static factory methods
        .def_static("from_data", [](
            nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> data,
            uint32_t width,
            uint32_t height,
            uint8_t channels,
            bool flip_x,
            bool flip_y,
            bool transpose,
            const std::string& name,
            const std::string& source_path,
            const std::string& uuid_hint
        ) {
            return TcTexture::from_data(
                data.data(),
                width, height, channels,
                flip_x, flip_y, transpose,
                name, source_path, uuid_hint
            );
        },
            nb::arg("data"),
            nb::arg("width"),
            nb::arg("height"),
            nb::arg("channels") = 4,
            nb::arg("flip_x") = false,
            nb::arg("flip_y") = true,
            nb::arg("transpose") = false,
            nb::arg("name") = "",
            nb::arg("source_path") = "",
            nb::arg("uuid") = ""
        )

        .def_static("white_1x1", &TcTexture::white_1x1)

        .def_static("from_uuid", &TcTexture::from_uuid, nb::arg("uuid"))

        .def_static("get_or_create", &TcTexture::get_or_create, nb::arg("uuid"))

        // Wrap a raw `tc_texture_handle` into a `TcTexture` instance. Used
        // by callers that already hold a handle from another binding (e.g.
        // `tc_render_target.color_texture`) and want to feed it into APIs
        // that expect `TcTexture` (material.set_texture, viewports, …).
        // Returns an invalid TcTexture if the handle is stale.
        .def_static("from_handle",
            [](uint32_t index, uint32_t generation) {
                tc_texture_handle h{index, generation};
                return TcTexture(h);
            },
            nb::arg("index"), nb::arg("generation"));

    // Alias for backwards compatibility
    m.attr("TextureData") = m.attr("TcTexture");

    // Registry functions
    m.def("tc_texture_count", []() { return tc_texture_count(); });

    m.def("tc_texture_get_all_info", []() {
        size_t count = 0;
        tc_texture_info* infos = tc_texture_get_all_info(&count);
        if (!infos) {
            return nb::list();
        }

        nb::list result;
        for (size_t i = 0; i < count; ++i) {
            nb::dict d;
            d["uuid"] = infos[i].uuid;
            d["name"] = infos[i].name ? infos[i].name : "";
            d["source_path"] = infos[i].source_path ? infos[i].source_path : "";
            d["ref_count"] = infos[i].ref_count;
            d["version"] = infos[i].version;
            d["width"] = infos[i].width;
            d["height"] = infos[i].height;
            d["channels"] = infos[i].channels;
            d["format"] = infos[i].format;
            d["is_loaded"] = (bool)infos[i].is_loaded;
            d["has_load_callback"] = (bool)infos[i].has_load_callback;
            d["memory_bytes"] = infos[i].memory_bytes;
            result.append(d);
        }

        free(infos);
        return result;
    });

    // Lazy loading API
    m.def("tc_texture_declare", [](const std::string& uuid, const std::string& name) {
        tc_texture_handle h = tc_texture_declare(uuid.c_str(), name.empty() ? nullptr : name.c_str());
        return TcTexture(h);
    }, nb::arg("uuid"), nb::arg("name") = "",
       "Declare a texture that will be loaded lazily");

    m.def("tc_texture_is_loaded", [](TcTexture& handle) {
        return tc_texture_is_loaded(handle.handle);
    }, nb::arg("handle"), "Check if texture data is loaded");

    m.def("tc_texture_ensure_loaded", [](TcTexture& handle) {
        return tc_texture_ensure_loaded(handle.handle);
    }, nb::arg("handle"), "Ensure texture is loaded (triggers callback if needed)");

    m.def("tc_texture_set_load_callback", [](TcTexture& handle, nb::callable callback) {
        tc_texture* tex = handle.get();
        if (!tex) {
            throw std::runtime_error("Invalid texture handle");
        }

        std::string uuid(tex->header.uuid);

        {
            std::lock_guard<std::mutex> lock(g_texture_callback_mutex);
            g_texture_python_callbacks[uuid] = callback;
        }

        tc_texture_set_load_callback(handle.handle, python_texture_load_callback_wrapper, nullptr);
    }, nb::arg("handle"), nb::arg("callback"),
       "Set Python callback for lazy texture loading");

    m.def("tc_texture_clear_load_callback", [](TcTexture& handle) {
        tc_texture* tex = handle.get();
        if (!tex) return;

        std::string uuid(tex->header.uuid);

        {
            std::lock_guard<std::mutex> lock(g_texture_callback_mutex);
            g_texture_python_callbacks.erase(uuid);
        }

        tc_texture_set_load_callback(handle.handle, nullptr, nullptr);
    }, nb::arg("handle"), "Clear load callback for texture");

    nb::module_ atexit = nb::module_::import_("atexit");
    atexit.attr("register")(nb::cpp_function(&cleanup_texture_callbacks));
}

} // namespace tgfx_bindings
