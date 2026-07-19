// sdl_backend_window.hpp - SDL-backed BackendWindow implementation.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include <SDL2/SDL.h>

#include "termin/platform/backend_window.hpp"

namespace termin {

class TERMIN_WINDOW_API SDLBackendWindow : public BackendWindow {
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SDL_Window* window_ = nullptr;
    bool should_close_ = false;

public:
    // Create a window + backend device. Throws std::runtime_error on
    // SDL / device failure. The selected render backend is TERMIN_BACKEND.
    SDLBackendWindow(
        const std::string& title,
        int width,
        int height,
        tgfx::PresentationMode presentation_mode = tgfx::PresentationMode::VSync);

    // Secondary-window constructor. Uses the IRenderDevice owned by
    // `share_with`; no new device is created.
    SDLBackendWindow(const std::string& title, int width, int height,
                     SDLBackendWindow& share_with);

    ~SDLBackendWindow() override;

    SDLBackendWindow(const SDLBackendWindow&) = delete;
    SDLBackendWindow& operator=(const SDLBackendWindow&) = delete;

    tgfx::IRenderDevice* device() override;
    tgfx::RenderContext2* context() override;
    tgfx::BackendType backend_type() const override;
    tgfx::PresentationMode requested_presentation_mode() const override;
    tgfx::PresentationMode presentation_mode() const override;

    SDL_Window* sdl_window() const { return window_; }

    bool should_close() const override { return should_close_; }
    void set_should_close(bool v) override { should_close_ = v; }

    void maximize() override;
    void set_title(const std::string& title) override;
    void set_icon_bmp(const std::string& path);
    void set_fullscreen(bool enabled) override;
    void set_text_input_enabled(bool enabled) override;
    void set_always_on_top(bool enabled);
    void close() override;
    bool poll_event(WindowEvent& out_event) override;
    std::pair<int, int> window_size() const override;
    std::pair<int, int> framebuffer_size() const override;
    void present(tgfx::TextureHandle color_tex) override;
};

} // namespace termin
