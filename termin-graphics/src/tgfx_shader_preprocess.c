#include <tgfx/tgfx_shader_preprocess.h>

static tgfx_shader_preprocess_fn g_shader_preprocess = 0;

void tgfx_gpu_set_shader_preprocess(tgfx_shader_preprocess_fn fn) {
    g_shader_preprocess = fn;
}

tgfx_shader_preprocess_fn tgfx_gpu_get_shader_preprocess(void) {
    return g_shader_preprocess;
}
