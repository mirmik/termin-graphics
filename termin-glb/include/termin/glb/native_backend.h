#pragma once

#include <stddef.h>

#if defined(_WIN32)
#if defined(TERMIN_GLB_EXPORTS)
#define TERMIN_GLB_API __declspec(dllexport)
#else
#define TERMIN_GLB_API __declspec(dllimport)
#endif
#else
#define TERMIN_GLB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TERMIN_GLB_CGLTF_VERSION "1.15"
#define TERMIN_GLB_CGLTF_REVISION "85cd62382dfea638278962690cf515023f33ed00"

typedef enum termin_glb_error_code {
    TERMIN_GLB_ERROR_NONE = 0,
    TERMIN_GLB_ERROR_IO,
    TERMIN_GLB_ERROR_INVALID_FORMAT,
    TERMIN_GLB_ERROR_UNSUPPORTED,
    TERMIN_GLB_ERROR_OUT_OF_MEMORY,
    TERMIN_GLB_ERROR_INTERNAL,
} termin_glb_error_code;

#define TERMIN_GLB_ERROR_MESSAGE_SIZE 512

typedef struct termin_glb_error {
    termin_glb_error_code code;
    char message[TERMIN_GLB_ERROR_MESSAGE_SIZE];
} termin_glb_error;

TERMIN_GLB_API const char* termin_glb_backend_name(void);
TERMIN_GLB_API const char* termin_glb_cgltf_version(void);
TERMIN_GLB_API const char* termin_glb_cgltf_revision(void);
TERMIN_GLB_API const char* termin_glb_error_code_name(termin_glb_error_code code);
TERMIN_GLB_API void termin_glb_error_clear(termin_glb_error* error);

#ifdef __cplusplus
}
#endif
