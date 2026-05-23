// tgfx_shader_preprocess.h - process-global GLSL preprocessor callback
#pragma once

#include "tgfx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Preprocess shader source (e.g. resolve #include directives).
// Returns malloc'd string (caller frees) or NULL if no preprocessing needed.
typedef char* (*tgfx_shader_preprocess_fn)(const char* source, const char* source_name);

TGFX_API void tgfx_gpu_set_shader_preprocess(tgfx_shader_preprocess_fn fn);
TGFX_API tgfx_shader_preprocess_fn tgfx_gpu_get_shader_preprocess(void);

#ifdef __cplusplus
}
#endif
