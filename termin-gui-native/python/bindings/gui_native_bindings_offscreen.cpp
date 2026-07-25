#include "gui_native_bindings_module.hpp"
#include "gui_native_bindings_shared.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>

#include <tcbase/tc_log.h>
#include <termin/gui_native/dynamic_texture_lease.hpp>
#include <termin/gui_native/offscreen_composition.hpp>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>

namespace {

using termin::gui_native::CanvasTextureLayer;
using termin::gui_native::DynamicTextureLease;
using termin::gui_native::DynamicTextureOwnership;
using termin::gui_native::OffscreenGuiComposition;
using termin::gui_native::OffscreenGuiCompositionConfig;
using termin::gui_native::TcDocument;
using CanvasRef = termin::gui_native::python_bindings::CanvasRef;
using ColorPickerRef = termin::gui_native::python_bindings::ColorPickerRef;
using Rgba8Array = nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu>;

struct Rgba8View {
    uint32_t width = 0;
    uint32_t height = 0;
    std::span<const uint8_t> pixels;
};

Rgba8View rgba8_view(const Rgba8Array& array, const char* operation) {
    if (array.ndim() != 3 || array.shape(2) != 4) {
        throw std::invalid_argument(
            std::string(operation) +
            " requires a C-contiguous uint8 array with shape (height, width, 4)");
    }
    if (array.shape(0) == 0 || array.shape(1) == 0 ||
        array.shape(0) > std::numeric_limits<uint32_t>::max() ||
        array.shape(1) > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument(
            std::string(operation) + " requires positive uint32-sized dimensions");
    }
    const auto height = static_cast<uint32_t>(array.shape(0));
    const auto width = static_cast<uint32_t>(array.shape(1));
    return Rgba8View{
        width,
        height,
        std::span<const uint8_t>(
            array.data(),
            static_cast<size_t>(width) * static_cast<size_t>(height) * 4),
    };
}

class PythonOffscreenGuiComposition {
  public:
    explicit PythonOffscreenGuiComposition(OffscreenGuiCompositionConfig config)
        : composition_(std::make_unique<OffscreenGuiComposition>(std::move(config))),
          document_(composition_->document()) {
        termin::gui_native::python_bindings::require_document_state(document_);
    }

    ~PythonOffscreenGuiComposition() {
        try {
            if (composition_) {
                tc_log_error(
                    "[termin-gui-native/python] OffscreenGuiComposition reached "
                    "finalization while open; closing it");
            }
            close();
        } catch (const std::exception& error) {
            tc_log_error(
                "[termin-gui-native/python] offscreen finalization failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[termin-gui-native/python] offscreen finalization failed with "
                "unknown exception");
        }
    }

    OffscreenGuiComposition& get() const {
        if (!composition_) {
            throw std::runtime_error("OffscreenGuiComposition is closed");
        }
        return *composition_;
    }

    TcDocument document() const {
        if (!document_.valid()) {
            throw std::runtime_error("OffscreenGuiComposition has no document");
        }
        return document_;
    }

    void close() {
        if (!composition_) return;
        termin::gui_native::python_bindings::release_document_state(document_);
        composition_->close();
        document_ = TcDocument{};
        composition_.reset();
    }

    bool is_open() const { return composition_ && composition_->is_open(); }

  private:
    std::unique_ptr<OffscreenGuiComposition> composition_;
    TcDocument document_;
};

tgfx::BackendType offscreen_backend(const std::string& name) {
    const tgfx::BackendType backend = tgfx::backend_from_name(name);
    if (backend == tgfx::BackendType::Null) {
        throw std::invalid_argument(
            "backend must name a supported offscreen backend such as "
            "'vulkan' or 'd3d11'");
    }
    return backend;
}

nb::ndarray<nb::numpy, float>
read_offscreen_frame(PythonOffscreenGuiComposition& self) {
    const auto [width, height] = self.get().latest_frame_size();
    std::vector<float> pixels = self.get().read_frame_rgba_float();
    auto* data = new float[pixels.size()];
    std::copy(pixels.begin(), pixels.end(), data);
    nb::capsule owner(data, [](void* pointer) noexcept {
        delete[] static_cast<float*>(pointer);
    });
    const size_t shape[] = {
        static_cast<size_t>(height),
        static_cast<size_t>(width),
        4,
    };
    return nb::ndarray<nb::numpy, float>(data, 3, shape, owner);
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

void bind_gui_native_offscreen(nb::module_& m) {
    nb::enum_<DynamicTextureOwnership>(m, "DynamicTextureOwnership")
        .value("EMPTY", DynamicTextureOwnership::Empty)
        .value("OWNED", DynamicTextureOwnership::Owned)
        .value("BORROWED", DynamicTextureOwnership::Borrowed)
        .value("RELEASED", DynamicTextureOwnership::Released);

    nb::enum_<CanvasTextureLayer>(m, "CanvasTextureLayer")
        .value("IMAGE", CanvasTextureLayer::Image)
        .value("OVERLAY", CanvasTextureLayer::Overlay);

    nb::class_<PythonOffscreenGuiComposition>(m, "OffscreenGuiComposition")
        .def(
            "__init__",
            [](PythonOffscreenGuiComposition* self, int width, int height,
               const std::string& backend, const std::string& font_path,
               int font_size, std::array<float, 4> clear_color,
               bool enable_text_input, bool continuous_rendering,
               bool application_graphics_domain,
               const std::string& sdk_root,
               const std::string& shader_compiler_path,
               const std::string& slang_compiler_path,
               const std::string& shader_cache_root,
               const std::string& shader_artifact_root,
               bool enable_shader_dev_compile) {
                OffscreenGuiCompositionConfig config;
                config.width = width;
                config.height = height;
                config.backend = offscreen_backend(backend);
                config.renderer.font_path = font_path;
                config.renderer.font_size = font_size;
                config.renderer.clear_color = clear_color;
                config.renderer.enable_text_input = enable_text_input;
                config.continuous_rendering = continuous_rendering;
                config.application_graphics_domain = application_graphics_domain;
                config.sdk_root = sdk_root;
                config.shader_compiler_path = shader_compiler_path;
                config.slang_compiler_path = slang_compiler_path;
                config.shader_cache_root = shader_cache_root;
                config.shader_artifact_root = shader_artifact_root;
                config.enable_shader_dev_compile = enable_shader_dev_compile;
                new (self) PythonOffscreenGuiComposition(std::move(config));
            },
            nb::arg("width") = 1280,
            nb::arg("height") = 720,
            nb::arg("backend") = "vulkan",
            nb::arg("font_path") = "",
            nb::arg("font_size") = 14,
            nb::arg("clear_color") =
                std::array<float, 4>{0.03f, 0.035f, 0.045f, 1.0f},
            nb::arg("enable_text_input") = true,
            nb::arg("continuous_rendering") = true,
            nb::arg("application_graphics_domain") = false,
            nb::arg("sdk_root") = "",
            nb::arg("shader_compiler_path") = "",
            nb::arg("slang_compiler_path") = "",
            nb::arg("shader_cache_root") = "",
            nb::arg("shader_artifact_root") = "",
            nb::arg("enable_shader_dev_compile") = true)
        .def_prop_ro("document", &PythonOffscreenGuiComposition::document)
        .def_prop_ro(
            "graphics",
            [](PythonOffscreenGuiComposition& self) -> tgfx::GraphicsHost& {
                return self.get().graphics();
            },
            nb::rv_policy::reference_internal)
        .def(
            "set_unhandled_key_handler",
            [](PythonOffscreenGuiComposition& self, nb::object callback) {
                set_unhandled_key_handler(
                    self.get().renderer(),
                    std::move(callback));
            },
            nb::arg("callback").none() = nb::none())
        .def(
            "register_color_picker",
            [](PythonOffscreenGuiComposition& self, const ColorPickerRef& picker) {
                self.get().renderer().register_color_picker(picker.get());
            })
        .def(
            "unregister_color_picker",
            [](PythonOffscreenGuiComposition& self, const ColorPickerRef& picker) {
                self.get().renderer().unregister_color_picker(picker.get());
            })
        .def(
            "set_clipboard_text",
            [](PythonOffscreenGuiComposition& self, const std::string& text) {
                return self.get().platform_services().set_clipboard_text(text);
            })
        .def("pump_events", [](PythonOffscreenGuiComposition& self) {
            return self.get().pump_input();
        })
        .def("render_frame", [](PythonOffscreenGuiComposition& self) {
            return self.get().render_frame();
        })
        .def("tick", [](PythonOffscreenGuiComposition& self) {
            return self.get().tick();
        })
        .def("resize", [](PythonOffscreenGuiComposition& self, int width, int height) {
            self.get().resize(width, height);
        })
        .def("read_frame_rgba_float", &read_offscreen_frame)
        .def(
            "push_pointer_move",
            [](PythonOffscreenGuiComposition& self, float x, float y,
               uint32_t modifiers) {
                self.get().push_pointer(tc_ui_pointer_event{
                    TC_UI_POINTER_MOVE,
                    x,
                    y,
                    0,
                    0,
                    static_cast<int32_t>(modifiers),
                    0.0f,
                    0.0f,
                });
            },
            nb::arg("x"),
            nb::arg("y"),
            nb::arg("modifiers") = 0)
        .def(
            "push_key",
            [](PythonOffscreenGuiComposition& self, int32_t key, bool pressed,
               uint32_t modifiers, bool repeat) {
                self.get().push_key(tc_ui_key_event{
                    pressed ? TC_UI_KEY_DOWN : TC_UI_KEY_UP,
                    key,
                    0,
                    static_cast<int32_t>(modifiers),
                    repeat,
                });
            },
            nb::arg("key"),
            nb::arg("pressed") = true,
            nb::arg("modifiers") = 0,
            nb::arg("repeat") = false)
        .def("push_text", [](PythonOffscreenGuiComposition& self,
                             const std::string& text) {
            self.get().push_text(text);
        })
        .def("request_repaint", [](PythonOffscreenGuiComposition& self) {
            self.get().request_repaint();
        })
        .def_prop_ro("repaint_requested", [](PythonOffscreenGuiComposition& self) {
            return self.get().repaint_requested();
        })
        .def_prop_ro("should_close", [](PythonOffscreenGuiComposition& self) {
            return self.get().should_close();
        })
        .def("request_close", [](PythonOffscreenGuiComposition& self) {
            self.get().request_close();
        })
        .def("wait_idle", [](PythonOffscreenGuiComposition& self) {
            self.get().wait_idle();
        })
        .def_prop_ro("framebuffer_size", [](PythonOffscreenGuiComposition& self) {
            const auto [width, height] = self.get().framebuffer_size();
            return std::array<int, 2>{width, height};
        })
        .def_prop_ro("frame_generation", [](PythonOffscreenGuiComposition& self) {
            return self.get().frame_generation();
        })
        .def_prop_ro("latest_frame_texture", [](PythonOffscreenGuiComposition& self) {
            return self.get().latest_frame_texture();
        })
        .def_prop_ro("latest_frame_size", [](PythonOffscreenGuiComposition& self) {
            const auto [width, height] = self.get().latest_frame_size();
            return std::array<int, 2>{width, height};
        })
        .def_prop_ro("clipboard_text", [](PythonOffscreenGuiComposition& self) {
            return self.get().platform_services().clipboard_text();
        })
        .def_prop_ro("text_input_enabled", [](PythonOffscreenGuiComposition& self) {
            return self.get().platform_services().text_input_enabled();
        })
        .def("close", &PythonOffscreenGuiComposition::close)
        .def_prop_ro("closed", [](const PythonOffscreenGuiComposition& self) {
            return !self.is_open();
        })
        .def(
            "__enter__",
            [](PythonOffscreenGuiComposition& self)
                -> PythonOffscreenGuiComposition& { return self; },
            nb::rv_policy::reference_internal)
        .def("__exit__", [](PythonOffscreenGuiComposition& self, nb::handle,
                            nb::handle, nb::handle) {
            self.close();
            return false;
        });

    nb::class_<DynamicTextureLease>(m, "DynamicTextureLease")
        .def(
            "__init__",
            [](DynamicTextureLease* self,
               PythonOffscreenGuiComposition& composition) {
                new (self) DynamicTextureLease(composition.get().renderer());
            },
            nb::arg("composition"),
            nb::keep_alive<1, 2>())
        .def("set_rgba8", [](DynamicTextureLease& self, const Rgba8Array& data) {
            const Rgba8View view = rgba8_view(data, "DynamicTextureLease.set_rgba8");
            self.set_rgba8(view.width, view.height, view.pixels);
        })
        .def(
            "update_region_rgba8",
            [](DynamicTextureLease& self, uint32_t x, uint32_t y,
               const Rgba8Array& data) {
                const Rgba8View view =
                    rgba8_view(data, "DynamicTextureLease.update_region_rgba8");
                self.update_region_rgba8(
                    x,
                    y,
                    view.width,
                    view.height,
                    view.pixels);
            })
        .def("borrow", &DynamicTextureLease::borrow)
        .def("bind_canvas", [](DynamicTextureLease& self, const CanvasRef& canvas,
                               CanvasTextureLayer layer) {
            self.bind_canvas(canvas.get(), layer);
        }, nb::arg("canvas"), nb::arg("layer") = CanvasTextureLayer::Image)
        .def("unbind_canvas", [](DynamicTextureLease& self, const CanvasRef& canvas,
                                 CanvasTextureLayer layer) {
            self.unbind_canvas(canvas.get(), layer);
        })
        .def("clear", &DynamicTextureLease::clear)
        .def("close", &DynamicTextureLease::release)
        .def_prop_ro("ownership", &DynamicTextureLease::ownership)
        .def_prop_ro("texture", &DynamicTextureLease::texture)
        .def_prop_ro("width", &DynamicTextureLease::width)
        .def_prop_ro("height", &DynamicTextureLease::height)
        .def_prop_ro("empty", &DynamicTextureLease::empty)
        .def_prop_ro("closed", &DynamicTextureLease::released);
}
