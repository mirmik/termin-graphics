#include "tgfx/tgfx_gpu_ops.h"

// Global GPU ops pointer (set by rendering backend)
static const tgfx_gpu_ops* g_gpu_ops = NULL;

void tgfx_gpu_set_ops(const tgfx_gpu_ops* ops) {
    g_gpu_ops = ops;
}

const tgfx_gpu_ops* tgfx_gpu_get_ops(void) {
    return g_gpu_ops;
}

bool tgfx_gpu_available(void) {
    return g_gpu_ops != NULL;
}
