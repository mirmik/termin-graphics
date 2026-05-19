#include <tgfx/tgfx2_interop.h>

#include <cstdint>

static void* g_tgfx2_device = nullptr;

void tgfx2_interop_set_device(void* device) {
    g_tgfx2_device = device;
}

void* tgfx2_interop_get_device(void) {
    return g_tgfx2_device;
}

#ifndef TGFX2_HAS_OPENGL
void tgfx2_gpu_ops_register(void) {
}

uint32_t tgfx2_interop_register_external_gl_texture(
    uint32_t gl_tex_id,
    uint32_t width,
    uint32_t height,
    int format,
    uint32_t usage)
{
    (void)gl_tex_id;
    (void)width;
    (void)height;
    (void)format;
    (void)usage;
    return 0;
}

void tgfx2_interop_destroy_texture_handle(uint32_t handle_id) {
    (void)handle_id;
}
#endif
