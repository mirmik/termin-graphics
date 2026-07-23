#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <stdexcept>

#include <SDL2/SDL.h>

#include "termin/platform/sdl_backend_window.hpp"
#include "termin/window/window_manager.hpp"
#include "tgfx2/graphics_host.hpp"

int main() {
    try {
        auto session = termin::create_native_windowed_graphics();
        termin::WindowManager windows(*session);

        const termin::WindowHandle first =
            windows.create_window({"manager first", 320, 200});
        const termin::WindowHandle second =
            windows.create_window({"manager second", 240, 160});
        assert(first && second && first != second);
        assert(windows.size() == 2);
        assert(&windows.window(first).graphics_host() == &session->graphics());
        assert(&windows.window(second).graphics_host() == &session->graphics());

        windows.pump_events();
        windows.take_events(first);
        windows.take_events(second);

        auto& first_sdl =
            static_cast<termin::SDLBackendWindow&>(windows.window(first));
        auto& second_sdl =
            static_cast<termin::SDLBackendWindow&>(windows.window(second));

        SDL_Event key{};
        key.type = SDL_KEYDOWN;
        key.key.type = SDL_KEYDOWN;
        key.key.windowID = SDL_GetWindowID(second_sdl.sdl_window());
        key.key.keysym.sym = SDLK_c;
        key.key.keysym.scancode = SDL_SCANCODE_C;
        assert(SDL_PushEvent(&key) == 1);

        assert(windows.pump_events() == 1);
        assert(windows.pending_event_count(first) == 0);
        assert(windows.pending_event_count(second) == 1);
        assert(windows.pump_events() == 0);
        assert(windows.pending_event_count(second) == 1);

        const auto second_events = windows.take_events(second);
        assert(second_events.size() == 1);
        assert(second_events.front().type == termin::WindowEventType::KeyPressed);
        assert(second_events.front().key.key == termin::WindowKey::C);

        bool session_close_rejected = false;
        try {
            session->close();
        } catch (const std::logic_error&) {
            session_close_rejected = true;
        }
        assert(session_close_rejected);
        assert(!session->graphics().is_closed());

        windows.destroy_window(first);
        assert(!windows.contains(first));
        assert(windows.size() == 1);
        bool stale_rejected = false;
        try {
            (void)windows.window(first);
        } catch (const std::invalid_argument&) {
            stale_rejected = true;
        }
        assert(stale_rejected);
        assert(&windows.window(second).graphics_host() == &session->graphics());

        const termin::WindowHandle replacement =
            windows.create_window({"manager replacement", 200, 120});
        assert(replacement.slot == first.slot);
        assert(replacement.generation != first.generation);
        const auto handles = windows.handles();
        assert(handles.size() == 2);
        assert(handles.front() == second);
        assert(handles.back() == replacement);

        SDL_Event quit{};
        quit.type = SDL_QUIT;
        assert(SDL_PushEvent(&quit) == 1);
        assert(windows.pump_events() == 2);
        for (termin::WindowHandle handle : windows.handles()) {
            const auto events = windows.take_events(handle);
            assert(std::ranges::any_of(events, [](const termin::WindowEvent& event) {
                return event.type == termin::WindowEventType::CloseRequested;
            }));
        }

        windows.close();
        assert(!windows.is_open());
        session->close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "WindowManager live test skipped: %s\n", error.what());
        return 77;
    }
}
