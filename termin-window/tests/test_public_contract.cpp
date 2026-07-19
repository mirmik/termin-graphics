#include <type_traits>

#include "termin/platform/backend_window.hpp"
#ifdef TERMIN_WINDOW_HAS_SDL
#include "termin/platform/sdl_backend_window.hpp"
#endif

int main() {
#ifdef TERMIN_WINDOW_HAS_SDL
    static_assert(std::is_base_of_v<termin::BackendWindow, termin::SDLBackendWindow>);
    static_assert(!std::is_copy_constructible_v<termin::SDLBackendWindow>);
#endif
    return 0;
}
