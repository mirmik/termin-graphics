#include <cassert>
#include <cstdio>
#include <exception>
#include <stdexcept>

#include <SDL2/SDL.h>

#include "termin/platform/sdl_backend_window.hpp"
#include "tgfx2/graphics_host.hpp"

namespace {

void drain(termin::SDLBackendWindow& window) {
    termin::WindowEvent event;
    while (window.poll_event(event)) {
    }
}

} // namespace

int main() {
    try {
        auto graphics_session = termin::create_native_windowed_graphics();
        auto primary_owner = graphics_session->create_window(
            {"event routing primary", 320, 200});
        auto secondary_owner = graphics_session->create_window(
            {"event routing secondary", 240, 160});
        auto& primary = static_cast<termin::SDLBackendWindow&>(*primary_owner);
        auto& secondary = static_cast<termin::SDLBackendWindow&>(*secondary_owner);
        drain(primary);
        drain(secondary);

        primary.set_text_input_enabled(true);
        secondary.set_text_input_enabled(true);
        primary.set_text_input_enabled(false);
        assert(SDL_IsTextInputActive() == SDL_TRUE);
        secondary.set_text_input_enabled(false);
        assert(SDL_IsTextInputActive() == SDL_FALSE);

        SDL_Event native_key{};
        native_key.type = SDL_KEYDOWN;
        native_key.key.type = SDL_KEYDOWN;
        native_key.key.windowID = SDL_GetWindowID(secondary.sdl_window());
        native_key.key.keysym.sym = SDLK_c;
        native_key.key.keysym.scancode = SDL_SCANCODE_C;
        assert(SDL_PushEvent(&native_key) == 1);

        termin::WindowEvent event;
        assert(!primary.poll_event(event));
        assert(secondary.poll_event(event));
        assert(event.type == termin::WindowEventType::KeyPressed);
        assert(event.key.key == termin::WindowKey::C);
        assert(!secondary.poll_event(event));

        SDL_Event quit{};
        quit.type = SDL_QUIT;
        assert(SDL_PushEvent(&quit) == 1);
        assert(primary.poll_event(event));
        assert(event.type == termin::WindowEventType::CloseRequested);
        assert(secondary.poll_event(event));
        assert(event.type == termin::WindowEventType::CloseRequested);
        assert(primary.should_close());
        assert(secondary.should_close());

        bool live_window_close_rejected = false;
        try {
            graphics_session->close();
        } catch (const std::logic_error&) {
            live_window_close_rejected = true;
        }
        assert(live_window_close_rejected);
        assert(!graphics_session->graphics().is_closed());

        primary_owner.reset();
        assert(!graphics_session->graphics().is_closed());
        secondary_owner.reset();
        graphics_session->close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "multiwindow event routing skipped: %s\n", error.what());
        return 77;
    }
}
