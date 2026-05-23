#include <tgfx/tgfx2_interop.h>
#include <tgfx2/i_render_device.hpp>

#include <cstdint>

static void* g_tgfx2_device = nullptr;

void tgfx2_interop_set_device(void* device) {
    g_tgfx2_device = device;
}

void* tgfx2_interop_get_device(void) {
    return g_tgfx2_device;
}

#ifndef TGFX2_HAS_OPENGL
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

void tgfx2_interop_blit_texture(
    uint32_t src_handle_id,
    uint32_t dst_handle_id,
    int width,
    int height)
{
    (void)src_handle_id;
    (void)dst_handle_id;
    (void)width;
    (void)height;
}
#endif
