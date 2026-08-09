#include <termin/glb/native_backend.h>

#include <nanobind/nanobind.h>

namespace nb = nanobind;

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
}
