#include <termin/glb/native_backend.h>

#include <cstring>

#include <cgltf.h>

const char* termin_glb_backend_name(void) {
    return "cgltf";
}

const char* termin_glb_cgltf_version(void) {
    return TERMIN_GLB_CGLTF_VERSION;
}

const char* termin_glb_cgltf_revision(void) {
    return TERMIN_GLB_CGLTF_REVISION;
}

const char* termin_glb_error_code_name(termin_glb_error_code code) {
    switch (code) {
    case TERMIN_GLB_ERROR_NONE:
        return "none";
    case TERMIN_GLB_ERROR_IO:
        return "io";
    case TERMIN_GLB_ERROR_INVALID_FORMAT:
        return "invalid_format";
    case TERMIN_GLB_ERROR_UNSUPPORTED:
        return "unsupported";
    case TERMIN_GLB_ERROR_OUT_OF_MEMORY:
        return "out_of_memory";
    case TERMIN_GLB_ERROR_INTERNAL:
        return "internal";
    }
    return "unknown";
}

void termin_glb_error_clear(termin_glb_error* error) {
    if (!error)
        return;
    std::memset(error, 0, sizeof(*error));
}

static_assert(sizeof(cgltf_size) == sizeof(size_t));
