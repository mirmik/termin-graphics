#include <memory>
#include <string>

#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include "termin/platform/backend_window.hpp"
#include "termin/platform/sdl_backend_window.hpp"
#include "termin/window/window_manager.hpp"
#include "tgfx2/device_factory.hpp"
#include "tgfx2/graphics_host.hpp"
#include "tgfx2/i_render_device.hpp"

#ifdef TERMIN_WINDOW_HAS_SDL
#include <SDL2/SDL.h>
#endif

namespace nb = nanobind;

namespace {

    using termin::BackendWindow;
    using termin::BackendWindowSystem;
    using termin::SDLBackendWindow;
    using termin::WindowConfig;
    using termin::WindowHandle;
    using termin::WindowHandleHash;
    using termin::WindowManager;
    using termin::WindowedGraphicsSession;

    std::string backend_name(const BackendWindow& window) {
        return std::string(tgfx::backend_name(window.backend_type()));
    }

} // namespace

NB_MODULE(_window_native, m) {
    nb::module_::import_("tgfx._tgfx_native");

    nb::enum_<tgfx::PresentationMode>(m, "PresentationMode")
        .value("VSYNC", tgfx::PresentationMode::VSync)
        .value("IMMEDIATE", tgfx::PresentationMode::Immediate);

    nb::class_<BackendWindow>(m, "BackendWindow")
        .def_prop_ro("backend", &backend_name)
        .def_prop_ro("requested_presentation_mode", &BackendWindow::requested_presentation_mode)
        .def_prop_ro("presentation_mode", &BackendWindow::presentation_mode)
        .def("should_close", &BackendWindow::should_close)
        .def("set_should_close", &BackendWindow::set_should_close, nb::arg("value"))
        .def("maximize", &BackendWindow::maximize)
        .def("set_title", &BackendWindow::set_title, nb::arg("title"))
        .def("set_size", &BackendWindow::set_size, nb::arg("width"), nb::arg("height"))
        .def("set_fullscreen", &BackendWindow::set_fullscreen, nb::arg("enabled"))
        .def("set_text_input_enabled", &BackendWindow::set_text_input_enabled, nb::arg("enabled"))
        .def("clipboard_text", &BackendWindow::clipboard_text)
        .def("set_clipboard_text", &BackendWindow::set_clipboard_text, nb::arg("text"))
        .def("close", &BackendWindow::close)
        .def("poll_events", &BackendWindow::poll_events)
        .def("window_size", &BackendWindow::window_size)
        .def("framebuffer_size", &BackendWindow::framebuffer_size)
        .def_prop_ro("content_scale", &BackendWindow::content_scale)
        .def("present", &BackendWindow::present, nb::arg("color_tex"));
    nb::class_<BackendWindowSystem>(m, "BackendWindowSystem");

    nb::class_<SDLBackendWindow, BackendWindow>(m, "SDLBackendWindow")
        .def("set_icon_bmp", &SDLBackendWindow::set_icon_bmp, nb::arg("path"))
        .def("set_always_on_top", &SDLBackendWindow::set_always_on_top, nb::arg("enabled"))
        .def(
            "window_id",
            [](SDLBackendWindow& self) -> uint32_t {
                SDL_Window* window = self.sdl_window();
                return window ? SDL_GetWindowID(window) : 0;
            });

    nb::class_<WindowedGraphicsSession>(m, "WindowedGraphicsSession")
        .def_static("create_native", &termin::create_native_windowed_graphics)
        .def(
            "create_window",
            [](WindowedGraphicsSession& self,
               const std::string& title,
               int width,
               int height,
               tgfx::PresentationMode presentation_mode) {
                auto window = self.create_window(WindowConfig{title, width, height, presentation_mode});
                return std::unique_ptr<SDLBackendWindow>(static_cast<SDLBackendWindow*>(window.release()));
            },
            nb::arg("title"),
            nb::arg("width"),
            nb::arg("height"),
            nb::arg("presentation_mode") = tgfx::PresentationMode::VSync,
            nb::keep_alive<0, 1>())
        .def_prop_ro(
            "graphics",
            [](WindowedGraphicsSession& self) -> tgfx::GraphicsHost& { return self.graphics(); },
            nb::rv_policy::reference_internal)
        .def_prop_ro(
            "backend",
            [](WindowedGraphicsSession& self) {
                return std::string(tgfx::backend_name(self.graphics().device().backend_type()));
            })
        .def("close", &WindowedGraphicsSession::close);

    nb::class_<WindowHandle>(m, "WindowHandle")
        .def_ro("slot", &WindowHandle::slot)
        .def_ro("generation", &WindowHandle::generation)
        .def("__bool__", [](WindowHandle self) { return static_cast<bool>(self); })
        .def("__eq__", [](WindowHandle self, WindowHandle other) { return self == other; })
        .def("__hash__", [](WindowHandle self) { return WindowHandleHash{}(self); })
        .def("__repr__", [](WindowHandle self) {
            return "WindowHandle(slot=" + std::to_string(self.slot) +
                   ", generation=" + std::to_string(self.generation) + ")";
        });

    nb::class_<WindowManager>(m, "WindowManager")
        .def(nb::init<WindowedGraphicsSession&>(), nb::arg("graphics_session"), nb::keep_alive<1, 2>())
        .def(
            "create_window",
            [](WindowManager& self,
               const std::string& title,
               int width,
               int height,
               tgfx::PresentationMode presentation_mode) {
                return self.create_window(WindowConfig{title, width, height, presentation_mode});
            },
            nb::arg("title"),
            nb::arg("width"),
            nb::arg("height"),
            nb::arg("presentation_mode") = tgfx::PresentationMode::VSync)
        .def("destroy_window", &WindowManager::destroy_window, nb::arg("handle"))
        .def("contains", &WindowManager::contains, nb::arg("handle"))
        .def_prop_ro("handles", &WindowManager::handles)
        .def_prop_ro("size", &WindowManager::size)
        .def(
            "window",
            [](WindowManager& self, WindowHandle handle) -> BackendWindow& { return self.window(handle); },
            nb::arg("handle"),
            nb::rv_policy::reference_internal)
        .def("pump_events", &WindowManager::pump_events)
        .def("pending_event_count", &WindowManager::pending_event_count, nb::arg("handle"))
        .def("close", &WindowManager::close)
        .def_prop_ro("is_open", &WindowManager::is_open);

    m.def("quit_sdl", []() {
#ifdef TERMIN_WINDOW_HAS_SDL
        SDL_Quit();
#endif
    });
}
