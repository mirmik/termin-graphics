// test_backend_window_d3d11_present.cpp - termin-window D3D11 present smoke.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>

#ifdef _WIN32
#include <cstdlib>
#endif

#include "termin/platform/sdl_backend_window.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/i_render_device.hpp"

namespace {

void force_d3d11_backend() {
#ifdef _WIN32
    _putenv_s("TERMIN_BACKEND", "d3d11");
#endif
}

bool close_enough(float a, float b) {
    return std::fabs(a - b) < 0.02f;
}

} // namespace

int main() {
    force_d3d11_backend();

    try {
        termin::SDLBackendWindow win("BackendWindow D3D11 present smoke", 320, 240);
        tgfx::IRenderDevice* dev = win.device();
        if (!dev) {
            std::fprintf(stderr, "BackendWindow D3D11 smoke: no render device\n");
            return 1;
        }
        if (dev->backend_type() != tgfx::BackendType::D3D11) {
            std::fprintf(stderr, "BackendWindow D3D11 smoke: backend is not D3D11\n");
            return 1;
        }

        auto [fb_w, fb_h] = win.framebuffer_size();
        if (fb_w <= 0 || fb_h <= 0) {
            std::fprintf(stderr, "BackendWindow D3D11 smoke: invalid framebuffer size %dx%d\n",
                         fb_w,
                         fb_h);
            return 1;
        }

        tgfx::TextureDesc rt_desc;
        rt_desc.width = static_cast<uint32_t>(fb_w);
        rt_desc.height = static_cast<uint32_t>(fb_h);
        rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        rt_desc.usage = tgfx::TextureUsage::ColorAttachment |
                        tgfx::TextureUsage::Sampled |
                        tgfx::TextureUsage::CopySrc;
        tgfx::TextureHandle rt = dev->create_texture(rt_desc);
        if (!rt) {
            std::fprintf(stderr, "BackendWindow D3D11 smoke: failed to create render target\n");
            return 1;
        }

        tgfx::RenderPassDesc pass;
        tgfx::ColorAttachmentDesc color;
        color.texture = rt;
        color.load = tgfx::LoadOp::Clear;
        color.clear_color[0] = 0.24f;
        color.clear_color[1] = 0.48f;
        color.clear_color[2] = 0.76f;
        color.clear_color[3] = 1.00f;
        pass.colors.push_back(color);

        auto cmd = dev->create_command_list();
        cmd->begin();
        cmd->begin_render_pass(pass);
        cmd->end_render_pass();
        cmd->end();
        dev->submit(*cmd);

        float rgba[4] = {};
        if (!dev->read_pixel_rgba8(rt, fb_w / 2, fb_h / 2, rgba)) {
            std::fprintf(stderr, "BackendWindow D3D11 smoke: render target readback failed\n");
            dev->destroy(rt);
            return 1;
        }
        if (!close_enough(rgba[0], 0.24f) ||
            !close_enough(rgba[1], 0.48f) ||
            !close_enough(rgba[2], 0.76f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "BackendWindow D3D11 smoke: unexpected pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0],
                         rgba[1],
                         rgba[2],
                         rgba[3]);
            dev->destroy(rt);
            return 1;
        }

        win.present(rt);
        dev->wait_idle();
        dev->destroy(rt);
        win.close();

        std::printf("BackendWindow D3D11 present smoke OK: %dx%d\n", fb_w, fb_h);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "BackendWindow D3D11 smoke failed: %s\n", e.what());
        if (std::strstr(e.what(), "No available video device") ||
            std::strstr(e.what(), "D3D11CreateDevice failed")) {
            return 77;
        }
        return 1;
    }
}
