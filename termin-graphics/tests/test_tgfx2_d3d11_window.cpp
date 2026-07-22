#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>

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
        auto swapchain = std::make_unique<tgfx::D3D11Swapchain>(
            device,
            hwnd,
            kWidth,
            kHeight,
            tgfx::PresentationMode::VSync);
        const bool tearing_supported = swapchain->tearing_supported();
        if (swapchain->requested_presentation_mode() != tgfx::PresentationMode::VSync ||
            swapchain->presentation_mode() != tgfx::PresentationMode::VSync ||
            swapchain->tearing_enabled()) {
            throw std::runtime_error("invalid D3D11 VSync presentation state");
        }
        DXGI_SWAP_CHAIN_DESC1 native_desc{};
        if (FAILED(swapchain->native_swapchain1()->GetDesc1(&native_desc)) ||
            native_desc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD ||
            native_desc.BufferCount < 2 ||
            (native_desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
            throw std::runtime_error("invalid D3D11 VSync flip-model descriptor");
        }

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
            swapchain->backbuffer_texture(),
            offscreen,
            termin::Bounds2i::from_size(kWidth, kHeight),
            termin::Bounds2i::from_size(kWidth, kHeight));

        float rgba[4] = {};
        if (!device.read_pixel_rgba8(swapchain->backbuffer_texture(), kWidth / 2, kHeight / 2, rgba)) {
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

        if (!swapchain->compose_and_present(offscreen)) {
            std::fprintf(stderr, "D3D11 window smoke: present failed\n");
            device.destroy(offscreen);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        const tgfx::TextureHandle stale_after_resize = swapchain->backbuffer_texture();
        swapchain->resize(kWidth + 16, kHeight + 16);
        if (device.get_texture(stale_after_resize) != nullptr ||
            !device.get_texture(swapchain->backbuffer_texture())) {
            throw std::runtime_error("D3D11 resize retained a stale backbuffer");
        }
        DXGI_SWAP_CHAIN_DESC1 resized_desc{};
        if (FAILED(swapchain->native_swapchain1()->GetDesc1(&resized_desc)) ||
            resized_desc.Flags != native_desc.Flags) {
            throw std::runtime_error("D3D11 resize changed swapchain flags");
        }

        const tgfx::TextureHandle released_on_shutdown = swapchain->backbuffer_texture();
        swapchain.reset();
        if (device.get_texture(released_on_shutdown) != nullptr) {
            throw std::runtime_error("D3D11 swapchain shutdown retained the backbuffer");
        }

        if (tearing_supported) {
            tgfx::D3D11Swapchain immediate_swapchain(
                device,
                hwnd,
                kWidth,
                kHeight,
                tgfx::PresentationMode::Immediate);
            DXGI_SWAP_CHAIN_DESC1 immediate_desc{};
            if (immediate_swapchain.presentation_mode() != tgfx::PresentationMode::Immediate ||
                !immediate_swapchain.tearing_enabled() ||
                FAILED(immediate_swapchain.native_swapchain1()->GetDesc1(&immediate_desc)) ||
                (immediate_desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) == 0 ||
                !immediate_swapchain.compose_and_present(offscreen)) {
                throw std::runtime_error("D3D11 Immediate presentation failed");
            }
        } else {
            bool rejected = false;
            try {
                tgfx::D3D11Swapchain immediate_swapchain(
                    device,
                    hwnd,
                    kWidth,
                    kHeight,
                    tgfx::PresentationMode::Immediate);
            } catch (const std::exception&) {
                rejected = true;
            }
            if (!rejected) {
                throw std::runtime_error("unsupported D3D11 Immediate presentation was accepted");
            }
        }

        device.wait_idle();
        device.destroy(offscreen);
        std::printf(
            "D3D11 window smoke OK: %dx%d tearing=%s\n",
            kWidth,
            kHeight,
            tearing_supported ? "supported" : "unsupported");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "D3D11 window smoke: %s\n", e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return std::strstr(e.what(), "D3D11CreateDevice failed") ? 77 : 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
#endif
}
