// texture_bindings.cpp - TcTexture Python bindings
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/ndarray.h>
#include <cstring>

#include "tgfx/tgfx_texture_handle.hpp"

namespace nb = nanobind;

using namespace termin;

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

        .def_static("get_or_create", &TcTexture::get_or_create, nb::arg("uuid"));

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
            d["memory_bytes"] = infos[i].memory_bytes;
            result.append(d);
        }

        free(infos);
        return result;
    });
}

} // namespace tgfx_bindings
