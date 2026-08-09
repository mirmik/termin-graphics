#include <termin/glb/native_backend.h>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <stdexcept>
#include <string>

namespace nb = nanobind;

namespace {

    std::runtime_error native_error(const termin_glb_error& error) {
        return std::runtime_error(std::string(termin_glb_error_code_name(error.code)) + ": " + error.message);
    }

    class NativeDocument {
    public:
        explicit NativeDocument(const std::string& path) {
            termin_glb_error error = {};
            document_ = termin_glb_document_open(path.c_str(), &error);
            if (!document_)
                throw native_error(error);
        }

        NativeDocument(const NativeDocument&) = delete;
        NativeDocument& operator=(const NativeDocument&) = delete;

        ~NativeDocument() {
            termin_glb_document_close(document_);
        }

        size_t mesh_count() const {
            return termin_glb_document_mesh_count(document_);
        }

        nb::dict mesh_info(size_t mesh_index) const {
            termin_glb_mesh_info info = {};
            termin_glb_error error = {};
            if (!termin_glb_document_mesh_info(document_, mesh_index, &info, &error))
                throw native_error(error);
            nb::dict result;
            result["name"] = info.name;
            result["primitive_count"] = info.primitive_count;
            result["vertex_count"] = info.vertex_count;
            result["index_count"] = info.index_count;
            return result;
        }

        nb::list meshes() const {
            nb::list result;
            for (size_t i = 0; i < mesh_count(); ++i)
                result.append(mesh_info(i));
            return result;
        }

        void build_static_mesh(size_t mesh_index,
                               const std::string& mesh_uuid,
                               const std::string& mesh_name,
                               bool convert_to_z_up) {
            termin_glb_error error = {};
            if (!termin_glb_document_build_static_mesh(document_,
                                                       mesh_index,
                                                       mesh_uuid.c_str(),
                                                       mesh_name.empty() ? nullptr : mesh_name.c_str(),
                                                       convert_to_z_up,
                                                       &error)) {
                throw native_error(error);
            }
        }

    private:
        termin_glb_document* document_ = nullptr;
    };

} // namespace

NB_MODULE(_glb_native, module) {
    module.doc() = "Native cgltf backend for termin-glb";

    nb::enum_<termin_glb_error_code>(module, "NativeErrorCode")
        .value("NONE", TERMIN_GLB_ERROR_NONE)
        .value("IO", TERMIN_GLB_ERROR_IO)
        .value("INVALID_FORMAT", TERMIN_GLB_ERROR_INVALID_FORMAT)
        .value("UNSUPPORTED", TERMIN_GLB_ERROR_UNSUPPORTED)
        .value("OUT_OF_MEMORY", TERMIN_GLB_ERROR_OUT_OF_MEMORY)
        .value("INTERNAL", TERMIN_GLB_ERROR_INTERNAL);

    module.def("error_code_name", &termin_glb_error_code_name, nb::arg("code"));
    module.def("backend_info", []() {
        nb::dict info;
        info["name"] = termin_glb_backend_name();
        info["cgltf_version"] = termin_glb_cgltf_version();
        info["cgltf_revision"] = termin_glb_cgltf_revision();
        return info;
    });

    nb::class_<NativeDocument>(module, "NativeDocument")
        .def(nb::init<const std::string&>(), nb::arg("path"))
        .def_prop_ro("mesh_count", &NativeDocument::mesh_count)
        .def_prop_ro("meshes", &NativeDocument::meshes)
        .def("mesh_info", &NativeDocument::mesh_info, nb::arg("mesh_index"))
        .def("build_static_mesh",
             &NativeDocument::build_static_mesh,
             nb::arg("mesh_index"),
             nb::arg("mesh_uuid"),
             nb::arg("mesh_name") = "",
             nb::arg("convert_to_z_up") = true);
}
