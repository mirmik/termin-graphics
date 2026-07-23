// sdl_backend_window.hpp - SDL-backed BackendWindow implementation.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include <SDL2/SDL.h>

#include "termin/platform/backend_window.hpp"

namespace termin {

class SDLWindowSystem;

class TERMIN_WINDOW_API SDLBackendWindow : public BackendWindow {
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SDL_Window* window_ = nullptr;
    bool should_close_ = false;
    bool text_input_enabled_ = false;

public:
    // Create one presentation window on an application-owned graphics
    // runtime. The runtime must outlive this window.
    SDLBackendWindow(
        SDLWindowSystem& window_system,
        tgfx::GraphicsHost& graphics,
        const std::string& title,
        int width,
        int height,
        tgfx::PresentationMode presentation_mode = tgfx::PresentationMode::VSync);

    ~SDLBackendWindow() override;

    SDLBackendWindow(const SDLBackendWindow&) = delete;
    SDLBackendWindow& operator=(const SDLBackendWindow&) = delete;

    tgfx::BackendType backend_type() const override;
    tgfx::GraphicsHost& graphics_host() const override;
    tgfx::PresentationMode requested_presentation_mode() const override;
    tgfx::PresentationMode presentation_mode() const override;

    SDL_Window* sdl_window() const { return window_; }

    bool should_close() const override { return should_close_; }
    void set_should_close(bool v) override { should_close_ = v; }

    void maximize() override;
    void set_title(const std::string& title) override;
    void set_size(int width, int height) override;
    void set_icon_bmp(const std::string& path);
    void set_fullscreen(bool enabled) override;
    void set_text_input_enabled(bool enabled) override;
    void set_cursor(WindowCursor cursor) override;
    std::string clipboard_text() const override;
    bool set_clipboard_text(const std::string& text) override;
    void set_always_on_top(bool enabled);
    void close() override;
    bool poll_event(WindowEvent& out_event) override;
    std::pair<int, int> window_size() const override;
    std::pair<int, int> framebuffer_size() const override;
    void present(tgfx::TextureHandle color_tex) override;
};

class TERMIN_WINDOW_API SDLWindowSystem : public BackendWindowSystem {
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void register_window();
    void unregister_window();
    friend class SDLBackendWindow;

public:
    SDLWindowSystem();
    ~SDLWindowSystem() override;

    SDLWindowSystem(const SDLWindowSystem&) = delete;
    SDLWindowSystem& operator=(const SDLWindowSystem&) = delete;

    std::unique_ptr<tgfx::GraphicsHost> create_graphics_host() override;
    BackendWindowPtr create_window(
        tgfx::GraphicsHost& graphics,
        const WindowConfig& config) override;
    size_t live_window_count() const override;
    void close(tgfx::GraphicsHost& graphics) override;
};

} // namespace termin
