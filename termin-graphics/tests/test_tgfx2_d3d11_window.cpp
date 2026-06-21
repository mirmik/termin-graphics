#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>

#ifdef TGFX2_HAS_D3D11
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include <SDL.h>
#include <SDL_syswm.h>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/d3d11/d3d11_render_device.hpp"
#include "tgfx2/d3d11/d3d11_swapchain.hpp"
#endif

int main() {
#ifndef TGFX2_HAS_D3D11
    std::printf("D3D11 backend not compiled, skipping test\n");
    return 0;
#else
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return std::strstr(SDL_GetError(), "No available video device") ? 77 : 1;
    }

    constexpr int kWidth = 320;
    constexpr int kHeight = 240;
    SDL_Window* window = SDL_CreateWindow(
        "tgfx2 D3D11 window smoke",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        kWidth,
        kHeight,
        SDL_WINDOW_SHOWN);
    if (!window) {
        const char* error = SDL_GetError();
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", error);
        SDL_Quit();
        return std::strstr(error, "No available video device") ? 77 : 1;
    }

    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if (!SDL_GetWindowWMInfo(window, &wm_info)) {
        std::fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    HWND hwnd = wm_info.info.win.window;
    if (!hwnd) {
        std::fprintf(stderr, "SDL window did not expose a Win32 HWND\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    try {
        tgfx::D3D11RenderDevice device;
        tgfx::D3D11Swapchain swapchain(device, hwnd, kWidth, kHeight);

        tgfx::TextureDesc offscreen_desc;
        offscreen_desc.width = kWidth;
        offscreen_desc.height = kHeight;
        offscreen_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        offscreen_desc.usage = tgfx::TextureUsage::ColorAttachment |
                               tgfx::TextureUsage::Sampled |
                               tgfx::TextureUsage::CopySrc;
        tgfx::TextureHandle offscreen = device.create_texture(offscreen_desc);
        if (!offscreen) {
            std::fprintf(stderr, "D3D11 window smoke: failed to create offscreen texture\n");
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        tgfx::RenderPassDesc pass;
        tgfx::ColorAttachmentDesc color;
        color.texture = offscreen;
        color.load = tgfx::LoadOp::Clear;
        color.clear_color[0] = 0.18f;
        color.clear_color[1] = 0.42f;
        color.clear_color[2] = 0.73f;
        color.clear_color[3] = 1.00f;
        pass.colors.push_back(color);

        auto cmd = device.create_command_list();
        cmd->begin();
        cmd->begin_render_pass(pass);
        cmd->end_render_pass();
        cmd->end();
        device.submit(*cmd);

        device.blit_to_texture(
            swapchain.backbuffer_texture(),
            offscreen,
            0,
            0,
            kWidth,
            kHeight,
            0,
            0,
            kWidth,
            kHeight);

        float rgba[4] = {};
        if (!device.read_pixel_rgba8(swapchain.backbuffer_texture(), kWidth / 2, kHeight / 2, rgba)) {
            std::fprintf(stderr, "D3D11 window smoke: backbuffer readback failed\n");
            device.destroy(offscreen);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        auto close_enough = [](float a, float b) {
            return std::fabs(a - b) < 0.02f;
        };
        if (!close_enough(rgba[0], 0.18f) ||
            !close_enough(rgba[1], 0.42f) ||
            !close_enough(rgba[2], 0.73f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(
                stderr,
                "D3D11 window smoke: unexpected backbuffer pixel %.3f %.3f %.3f %.3f\n",
                rgba[0],
                rgba[1],
                rgba[2],
                rgba[3]);
            device.destroy(offscreen);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        if (!swapchain.compose_and_present(offscreen, 0)) {
            std::fprintf(stderr, "D3D11 window smoke: present failed\n");
            device.destroy(offscreen);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        device.wait_idle();
        device.destroy(offscreen);
        std::printf("D3D11 window smoke OK: %ux%u\n", swapchain.width(), swapchain.height());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "D3D11 window smoke: %s\n", e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
#endif
}
