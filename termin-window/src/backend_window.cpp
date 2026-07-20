#include "termin/platform/backend_window.hpp"

#include <stdexcept>

#include "tgfx2/graphics_host.hpp"

extern "C" {
#include <tcbase/tc_log.h>
}

#ifdef TERMIN_WINDOW_HAS_SDL
#include "termin/platform/sdl_backend_window.hpp"
#endif

namespace termin {

void BackendWindow::poll_events() {
    WindowEvent event;
    while (poll_event(event)) {
        if (event_handler_) {
            event_handler_(event);
        }
    }
}

void BackendWindow::set_event_handler(
    std::function<void(const WindowEvent&)> handler) {
    event_handler_ = std::move(handler);
}

WindowedGraphicsSession::WindowedGraphicsSession(
    BackendWindowSystemPtr windows,
    std::unique_ptr<tgfx::GraphicsHost> graphics)
    : windows_(std::move(windows)), graphics_(std::move(graphics)) {
    if (!windows_ || !graphics_) {
        throw std::invalid_argument(
            "WindowedGraphicsSession requires a window system and GraphicsHost");
    }
}

WindowedGraphicsSession::~WindowedGraphicsSession() {
    if (closed_) return;
    if (windows_ && windows_->live_window_count() != 0) {
        tc_log_error(
            "[WindowedGraphicsSession] destroyed with %zu live presentation window(s)",
            windows_->live_window_count());
        return;
    }
    try {
        close();
    } catch (const std::exception& error) {
        tc_log_error("[WindowedGraphicsSession] shutdown failed: %s", error.what());
    }
}

tgfx::GraphicsHost& WindowedGraphicsSession::graphics() {
    if (!graphics_ || graphics_->is_closed()) {
        throw std::logic_error("WindowedGraphicsSession: graphics host is closed");
    }
    return *graphics_;
}

const tgfx::GraphicsHost& WindowedGraphicsSession::graphics() const {
    if (!graphics_ || graphics_->is_closed()) {
        throw std::logic_error("WindowedGraphicsSession: graphics host is closed");
    }
    return *graphics_;
}

BackendWindowPtr WindowedGraphicsSession::create_window(const WindowConfig& config) {
    if (closed_) {
        throw std::logic_error("WindowedGraphicsSession::create_window called after close");
    }
    return windows_->create_window(graphics(), config);
}

void WindowedGraphicsSession::close() {
    if (closed_) return;
    if (windows_->live_window_count() != 0) {
        throw std::logic_error(
            "WindowedGraphicsSession::close requires all presentation windows to be closed");
    }
    windows_->close(*graphics_);
    closed_ = true;
}

BackendWindowSystemPtr create_native_window_system() {
#ifdef TERMIN_WINDOW_HAS_SDL
    return std::make_unique<SDLWindowSystem>();
#else
    throw std::runtime_error(
        "create_native_window_system: this termin-window build has no native backend");
#endif
}

std::unique_ptr<WindowedGraphicsSession> create_native_windowed_graphics() {
    BackendWindowSystemPtr windows = create_native_window_system();
    std::unique_ptr<tgfx::GraphicsHost> graphics = windows->create_graphics_host();
    return std::make_unique<WindowedGraphicsSession>(
        std::move(windows), std::move(graphics));
}

} // namespace termin
