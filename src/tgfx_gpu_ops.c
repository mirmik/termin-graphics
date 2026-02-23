#include "tgfx/tgfx_gpu_ops.h"

// Global GPU ops pointer (set by rendering backend)
static const tgfx_gpu_ops* g_gpu_ops = NULL;

// Separate shader preprocess callback
static tgfx_shader_preprocess_fn g_shader_preprocess = NULL;

void tgfx_gpu_set_ops(const tgfx_gpu_ops* ops) {
    g_gpu_ops = ops;
}

const tgfx_gpu_ops* tgfx_gpu_get_ops(void) {
    return g_gpu_ops;
}

void tgfx_gpu_set_shader_preprocess(tgfx_shader_preprocess_fn fn) {
    g_shader_preprocess = fn;
}

bool tgfx_gpu_available(void) {
    return g_gpu_ops != NULL;
}
