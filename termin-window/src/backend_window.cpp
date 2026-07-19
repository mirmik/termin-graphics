#include "termin/platform/backend_window.hpp"

#include <stdexcept>

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

BackendWindowPtr create_native_window(const WindowConfig& config) {
#ifdef TERMIN_WINDOW_HAS_SDL
    return std::make_unique<SDLBackendWindow>(
        config.title,
        config.width,
        config.height,
        config.presentation_mode);
#else
    (void)config;
    throw std::runtime_error(
        "create_native_window: this termin-window build has no native window backend");
#endif
}

} // namespace termin
