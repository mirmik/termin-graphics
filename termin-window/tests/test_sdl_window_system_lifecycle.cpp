#include <cstdio>
#include <exception>
#include <memory>

#include <SDL2/SDL.h>

#include "termin/platform/sdl_backend_window.hpp"
#include "tgfx2/graphics_host.hpp"

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "SDL window-system lifecycle failed: %s\n", message);
    }
    return condition;
}

bool has_current_gl_bootstrap() {
    return SDL_GL_GetCurrentContext() != nullptr &&
        SDL_GL_GetCurrentWindow() != nullptr;
}

bool has_no_current_gl_bootstrap() {
    return SDL_GL_GetCurrentContext() == nullptr &&
        SDL_GL_GetCurrentWindow() == nullptr;
}

} // namespace

int main() {
    try {
        // Destruction before create_graphics_host() owns the prepared device
        // and must release it before deleting the bootstrap context/window.
        {
            auto windows = std::make_unique<termin::SDLWindowSystem>();
            if (!check(has_current_gl_bootstrap(),
                    "constructor did not install its GL bootstrap context")) return 1;
        }
        if (!check(has_no_current_gl_bootstrap(),
                "destructor left the prepared-device bootstrap current")) return 1;

        // The fallback remains valid after ownership of the device moved to
        // GraphicsHost, provided the external owner has already closed it.
        std::unique_ptr<tgfx::GraphicsHost> graphics;
        {
            auto windows = std::make_unique<termin::SDLWindowSystem>();
            graphics = windows->create_graphics_host();
            graphics->close();
            if (!check(has_current_gl_bootstrap(),
                    "closed GraphicsHost unexpectedly removed the platform bootstrap")) return 1;
        }
        if (!check(has_no_current_gl_bootstrap(),
                "destructor fallback left the adopted-device bootstrap current")) return 1;
        graphics.reset();

        // The regular close path shares the same idempotent resource release.
        {
            auto windows = std::make_unique<termin::SDLWindowSystem>();
            auto regular_graphics = windows->create_graphics_host();
            windows->close(*regular_graphics);
            if (!check(has_no_current_gl_bootstrap(),
                    "close left the GL bootstrap current")) return 1;
        }
        if (!check(has_no_current_gl_bootstrap(),
                "destructor repeated close incorrectly")) return 1;
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SDL window-system lifecycle skipped: %s\n", error.what());
        return 77;
    }
}
