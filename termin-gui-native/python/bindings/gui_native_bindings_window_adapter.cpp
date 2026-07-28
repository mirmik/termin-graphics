#include "gui_native_bindings_module.hpp"
#include "gui_native_bindings_shared.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <nanobind/stl/array.h>

#include <termin/gui_native/dynamic_texture_lease.hpp>
#include <termin/gui_native/window_adapter.hpp>
#include <termin/window/window_manager.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/render_context.hpp>

namespace {

using termin::WindowHandle;
using termin::WindowManager;
using termin::gui_native::ColorPicker;
using termin::gui_native::DocumentRendererConfig;
using termin::gui_native::DynamicTextureLease;
using termin::gui_native::GuiWindowAdapter;
using termin::gui_native::TcDocument;
using WidgetHandleRef = termin::gui_native::python_bindings::WidgetHandle;

ColorPicker& require_color_picker(
    GuiWindowAdapter& adapter,
    const nb::object& picker) {
    WidgetHandleRef handle = nb::cast<WidgetHandleRef>(picker.attr("handle"));
    tc_widget* widget = tc_ui_document_resolve_widget(
        adapter.document().handle(),
        handle.handle);
    if (!widget || !widget->body) {
        throw std::runtime_error("color picker reference is stale");
    }
    auto* color_picker = dynamic_cast<ColorPicker*>(
        static_cast<termin::gui_native::Widget*>(widget->body));
    if (!color_picker) {
        throw std::invalid_argument("widget is not a ColorPicker");
    }
    return *color_picker;
}

int32_t python_key_code(termin::WindowKey key) {
    return termin::window_key_code(key);
}

nb::dict python_window_event(const termin::WindowEvent& event) {
    nb::dict result;
    switch (event.type) {
    case termin::WindowEventType::KeyPressed:
    case termin::WindowEventType::KeyReleased:
        result["type"] = event.type == termin::WindowEventType::KeyPressed
            ? "key_down"
            : "key_up";
        result["key"] = python_key_code(event.key.key);
        result["native_key"] = event.key.native_key;
        result["scancode"] = event.key.native_scancode;
        result["mods"] = event.key.modifiers;
        result["repeat"] = event.key.repeat;
        break;
    case termin::WindowEventType::FileDropped:
        result["type"] = "file_drop";
        result["path"] = event.file_drop.path;
        result["x"] = event.file_drop.logical_position.x;
        result["y"] = event.file_drop.logical_position.y;
        result["mods"] = event.file_drop.modifiers;
        break;
    case termin::WindowEventType::CloseRequested:
        result["type"] = "window_close";
        break;
    default:
        result["type"] = "other";
        break;
    }
    return result;
}

void set_unhandled_key_handler(
    termin::gui_native::DocumentRenderer& renderer,
    nb::object callback) {
    if (!callback.is_none() && !nb::isinstance<nb::callable>(callback)) {
        throw std::invalid_argument(
            "unhandled key handler must be callable or None");
    }
    if (callback.is_none()) {
        renderer.set_unhandled_key_handler({});
        return;
    }
    renderer.set_unhandled_key_handler(
        [callback = std::move(callback)](const tc_ui_key_event& event) {
            nb::gil_scoped_acquire gil;
            return nb::cast<bool>(callback(event.key, event.modifiers));
        });
}

} // namespace

void bind_gui_native_window_adapter(nb::module_& m) {
    nb::class_<GuiWindowAdapter>(m, "GuiWindowAdapter")
        .def(
            "__init__",
            [](GuiWindowAdapter* self, WindowManager& manager,
               WindowHandle handle, TcDocument document,
               const std::string& font_path, int font_size,
               std::array<float, 4> clear_color, bool enable_text_input) {
                if (font_size <= 0) {
                    throw std::invalid_argument("font_size must be positive");
                }
                DocumentRendererConfig config;
                config.font_path = font_path;
                config.font_size = font_size;
                config.clear_color = clear_color;
                config.enable_text_input = enable_text_input;
                termin::BackendWindow& window = manager.window(handle);
                new (self) GuiWindowAdapter(
                    window.graphics_host(),
                    document,
                    std::move(config),
                    window);
            },
            nb::arg("window_manager"),
            nb::arg("handle"),
            nb::arg("document"),
            nb::arg("font_path"),
            nb::arg("font_size") = 14,
            nb::arg("clear_color") =
                std::array<float, 4>{0.03f, 0.035f, 0.045f, 1.0f},
            nb::arg("enable_text_input") = true,
            nb::keep_alive<1, 2>(),
            nb::keep_alive<1, 4>())
        .def(
            "consume_pending_events",
            [](GuiWindowAdapter& self, WindowManager& manager,
               WindowHandle handle, nb::object interceptor) {
                if (!interceptor.is_none() &&
                    !nb::isinstance<nb::callable>(interceptor)) {
                    throw std::invalid_argument(
                        "event interceptor must be callable or None");
                }
                if (&manager.window(handle) != &self.window()) {
                    throw std::invalid_argument(
                        "window handle does not identify this adapter window");
                }
                std::vector<termin::WindowEvent> events =
                    manager.take_events(handle);
                std::vector<termin::WindowEvent> unconsumed;
                for (const termin::WindowEvent& event : events) {
                    const bool consumed = !interceptor.is_none() &&
                        nb::cast<bool>(interceptor(python_window_event(event)));
                    if (!consumed) unconsumed.push_back(event);
                }
                self.consume_events(unconsumed);
                if (!events.empty()) self.request_repaint();
                return events.size();
            },
            nb::arg("window_manager"),
            nb::arg("handle"),
            nb::arg("interceptor").none() = nb::none())
        .def(
            "set_unhandled_key_handler",
            [](GuiWindowAdapter& self, nb::object callback) {
                set_unhandled_key_handler(
                    self.renderer(),
                    std::move(callback));
            },
            nb::arg("callback").none() = nb::none())
        .def(
            "set_before_frame_callback",
            [](GuiWindowAdapter& self, nb::object callback) {
                if (!callback.is_none() &&
                    !nb::isinstance<nb::callable>(callback)) {
                    throw std::invalid_argument(
                        "before-frame callback must be callable or None");
                }
                if (callback.is_none()) {
                    self.renderer().set_before_frame_callback({});
                    return;
                }
                self.renderer().set_before_frame_callback(
                    [callback = std::move(callback)](tgfx::RenderContext2&) {
                        nb::gil_scoped_acquire gil;
                        callback();
                    });
            },
            nb::arg("callback").none() = nb::none())
        .def("register_color_picker", [](GuiWindowAdapter& self,
                                          const nb::object& picker) {
            self.renderer().register_color_picker(
                require_color_picker(self, picker));
        })
        .def("unregister_color_picker", [](GuiWindowAdapter& self,
                                            const nb::object& picker) {
            self.renderer().unregister_color_picker(
                require_color_picker(self, picker));
        })
        .def("render_frame", &GuiWindowAdapter::render_and_present)
        .def("request_repaint", &GuiWindowAdapter::request_repaint)
        .def_prop_ro("repaint_requested", &GuiWindowAdapter::repaint_requested)
        .def_prop_ro("should_close", &GuiWindowAdapter::should_close)
        .def("wait_idle", &GuiWindowAdapter::wait_idle)
        .def("close", &GuiWindowAdapter::close)
        .def_prop_ro("closed", [](GuiWindowAdapter& self) {
            return !self.is_open();
        })
        .def_prop_ro("rendered_frame_count", [](GuiWindowAdapter& self) {
            return self.renderer().rendered_frame_count();
        })
        .def_prop_ro("framebuffer_size", [](GuiWindowAdapter& self) {
            const auto [width, height] = self.renderer().framebuffer_size();
            return std::array<int, 2>{width, height};
        })
        .def_prop_ro("color_target", [](GuiWindowAdapter& self) {
            return self.renderer().color_target();
        })
        .def_prop_ro(
            "window",
            [](GuiWindowAdapter& self) -> termin::BackendWindow& {
                return self.window();
            },
            nb::rv_policy::reference_internal)
        .def_prop_ro(
            "graphics",
            [](GuiWindowAdapter& self) -> tgfx::GraphicsHost& {
                return self.renderer().graphics();
            },
            nb::rv_policy::reference_internal);

    m.def(
        "dynamic_texture_lease",
        [](GuiWindowAdapter& adapter) -> DynamicTextureLease* {
            return new DynamicTextureLease(adapter.renderer());
        },
        nb::arg("adapter"),
        nb::rv_policy::take_ownership,
        nb::keep_alive<0, 1>());
}
