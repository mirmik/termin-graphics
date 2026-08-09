#include <termin/glb/native_backend.h>

#include <cstring>

int main() {
    if (std::strcmp(termin_glb_backend_name(), "cgltf") != 0)
        return 1;
    if (std::strcmp(termin_glb_cgltf_version(), TERMIN_GLB_CGLTF_VERSION) != 0)
        return 2;
    if (std::strcmp(termin_glb_cgltf_revision(), TERMIN_GLB_CGLTF_REVISION) != 0)
        return 3;
    if (std::strcmp(termin_glb_error_code_name(TERMIN_GLB_ERROR_UNSUPPORTED), "unsupported") != 0)
        return 4;

    termin_glb_error error{TERMIN_GLB_ERROR_INTERNAL, "failure"};
    termin_glb_error_clear(&error);
    if (error.code != TERMIN_GLB_ERROR_NONE || error.message[0] != '\0')
        return 5;
    return 0;
}
