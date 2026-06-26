// tc_opengl.h - OpenGL backend initialization for the C API.
#pragma once

#include <stdbool.h>

#include <tgfx/tgfx_api.h>

#ifdef __cplusplus
extern "C" {
#endif

TGFX_API bool tc_opengl_init(void);
TGFX_API bool tc_opengl_is_initialized(void);
TGFX_API void tc_opengl_shutdown(void);

#ifdef __cplusplus
}
#endif
