#include "gui_native_bindings_module.hpp"
#include "gui_native_bindings_shared.hpp"

#include <array>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>

#include <tcbase/tc_log.h>
#include <termin/gui_native/application_host.hpp>
#include <termin/gui_native/dynamic_texture_lease.hpp>
#include <tgfx2/graphics_host.hpp>

namespace {

using termin::WindowedGraphicsSession;
using termin::gui_native::GuiWindowHost;
using termin::gui_native::GuiWindowConfig;
using termin::gui_native::CanvasTextureLayer;
using termin::gui_native::DynamicTextureLease;
using termin::gui_native::DynamicTextureOwnership;
using termin::gui_native::StandaloneGuiApplication;
using termin::gui_native::StandaloneGuiApplicationConfig;
using CanvasRef = termin::gui_native::python_bindings::CanvasRef;
using PythonDocument = termin::gui_native::python_bindings::Document;

using Rgba8Array =
    nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu>;

struct Rgba8View {
  uint32_t width = 0;
  uint32_t height = 0;
  std::span<const uint8_t> pixels;
};

Rgba8View rgba8_view(const Rgba8Array &array, const char *operation) {
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
          static_cast<size_t>(width) * static_cast<size_t>(height) * 4)};
}

GuiWindowConfig make_window_config(
    std::string title, int width, int height,
    tgfx::PresentationMode presentation_mode, std::string font_path,
    int font_size, std::array<float, 4> clear_color, bool enable_text_input,
    bool continuous_rendering) {
  if (width <= 0 || height <= 0)
    throw std::invalid_argument("window dimensions must be positive");
  if (font_size <= 0)
    throw std::invalid_argument("font_size must be positive");
  GuiWindowConfig config;
  config.window = termin::WindowConfig{std::move(title), width, height,
                                       presentation_mode};
  config.font_path = std::move(font_path);
  config.font_size = font_size;
  config.clear_color = clear_color;
  config.enable_text_input = enable_text_input;
  config.continuous_rendering = continuous_rendering;
  return config;
}

class PythonGuiWindowHost {
public:
  PythonGuiWindowHost(nb::object session_object, nb::object document_object,
                      GuiWindowConfig config)
      : session_keepalive_(std::move(session_object)),
        document_keepalive_(std::move(document_object)) {
    auto &session = nb::cast<WindowedGraphicsSession &>(session_keepalive_);
    auto &document = nb::cast<PythonDocument &>(document_keepalive_);
    owned_ = std::make_unique<GuiWindowHost>(
        session, document.native_document(), std::move(config));
    host_ = owned_.get();
  }

  explicit PythonGuiWindowHost(GuiWindowHost &host) : host_(&host) {}

  ~PythonGuiWindowHost() {
    try {
      if (host_ && host_->is_open()) {
        tc_log_error("[termin-gui-native/python] GuiWindowHost reached "
                     "finalization while still open; closing it");
      }
      close();
    } catch (const std::exception &error) {
      tc_log_error(
          "[termin-gui-native/python] GuiWindowHost finalization failed: %s",
          error.what());
    } catch (...) {
      tc_log_error("[termin-gui-native/python] GuiWindowHost finalization "
                   "failed with unknown exception");
    }
  }

  GuiWindowHost &get() const {
    if (!host_)
      throw std::runtime_error("GuiWindowHost is closed");
    return *host_;
  }

  void close() {
    if (!host_)
      return;
    get().close();
    if (owned_)
      owned_.reset();
    host_ = nullptr;
    session_keepalive_ = nb::none();
    document_keepalive_ = nb::none();
  }

  void invalidate_borrowed() noexcept { host_ = nullptr; }

private:
  std::unique_ptr<GuiWindowHost> owned_;
  GuiWindowHost *host_ = nullptr;
  nb::object session_keepalive_ = nb::none();
  nb::object document_keepalive_ = nb::none();
};

class PythonStandaloneGuiApplication {
public:
  explicit PythonStandaloneGuiApplication(StandaloneGuiApplicationConfig config)
      : application_(
            std::make_unique<StandaloneGuiApplication>(std::move(config))),
        document_(std::make_unique<PythonDocument>(application_->document())),
        window_host_(
            std::make_unique<PythonGuiWindowHost>(application_->window_host())) {}

  ~PythonStandaloneGuiApplication() {
    try {
      if (is_open()) {
        tc_log_error("[termin-gui-native/python] StandaloneGuiApplication "
                     "reached finalization while still open; closing it");
      }
      close();
    } catch (const std::exception &error) {
      tc_log_error("[termin-gui-native/python] StandaloneGuiApplication "
                   "finalization failed: %s",
                   error.what());
    } catch (...) {
      tc_log_error("[termin-gui-native/python] StandaloneGuiApplication "
                   "finalization failed with unknown exception");
    }
  }

  void close() {
    if (!application_)
      return;
    application_->close();
    window_host_->invalidate_borrowed();
    document_->invalidate_borrowed();
    application_.reset();
  }

  bool is_open() const { return application_ && application_->is_open(); }

  PythonDocument &document() const {
    if (!document_)
      throw std::runtime_error("StandaloneGuiApplication has no document");
    return *document_;
  }

  PythonGuiWindowHost &window_host() const {
    if (!window_host_)
      throw std::runtime_error("StandaloneGuiApplication has no window host");
    return *window_host_;
  }

private:
  std::unique_ptr<StandaloneGuiApplication> application_;
  std::unique_ptr<PythonDocument> document_;
  std::unique_ptr<PythonGuiWindowHost> window_host_;
};

} // namespace

void bind_gui_native_application_host(nb::module_ &m) {
  nb::enum_<DynamicTextureOwnership>(m, "DynamicTextureOwnership")
      .value("EMPTY", DynamicTextureOwnership::Empty)
      .value("OWNED", DynamicTextureOwnership::Owned)
      .value("BORROWED", DynamicTextureOwnership::Borrowed)
      .value("RELEASED", DynamicTextureOwnership::Released);

  nb::enum_<CanvasTextureLayer>(m, "CanvasTextureLayer")
      .value("IMAGE", CanvasTextureLayer::Image)
      .value("OVERLAY", CanvasTextureLayer::Overlay);

  nb::class_<PythonGuiWindowHost>(m, "GuiWindowHost")
      .def(
          "__init__",
          [](PythonGuiWindowHost *self, nb::object session, nb::object document,
             const std::string &title, int width, int height,
             tgfx::PresentationMode presentation_mode,
             const std::string &font_path, int font_size,
             std::array<float, 4> clear_color, bool enable_text_input,
             bool continuous_rendering) {
            new (self) PythonGuiWindowHost(
                std::move(session), std::move(document),
                make_window_config(title, width, height, presentation_mode,
                                   font_path, font_size, clear_color,
                                   enable_text_input, continuous_rendering));
          },
          nb::arg("graphics_session"), nb::arg("document"),
          nb::arg("title") = "Termin", nb::arg("width") = 1280,
          nb::arg("height") = 720,
          nb::arg("presentation_mode") = tgfx::PresentationMode::VSync,
          nb::arg("font_path") = "", nb::arg("font_size") = 14,
          nb::arg("clear_color") =
              std::array<float, 4>{0.03f, 0.035f, 0.045f, 1.0f},
          nb::arg("enable_text_input") = true,
          nb::arg("continuous_rendering") = true)
      .def("pump_events",
           [](PythonGuiWindowHost &self) { return self.get().pump_events(); })
      .def("render_frame",
           [](PythonGuiWindowHost &self) { return self.get().render_frame(); })
      .def("tick", [](PythonGuiWindowHost &self) { return self.get().tick(); })
      .def("defer",
           [](PythonGuiWindowHost &self, nb::object callback) {
             if (!nb::isinstance<nb::callable>(callback))
               throw std::invalid_argument("callback must be callable");
             self.get().defer([callback = std::move(callback)]() {
               nb::gil_scoped_acquire gil;
               callback();
             });
           })
      .def("request_repaint", [](PythonGuiWindowHost &self) {
        self.get().request_repaint();
      })
      .def_prop_ro("repaint_requested", [](PythonGuiWindowHost &self) {
        return self.get().repaint_requested();
      })
      .def_prop_ro("should_close", [](PythonGuiWindowHost &self) {
        return self.get().should_close();
      })
      .def("request_close",
           [](PythonGuiWindowHost &self) { self.get().request_close(); })
      .def("wait_idle",
           [](PythonGuiWindowHost &self) { self.get().wait_idle(); })
      .def("close", &PythonGuiWindowHost::close)
      .def_prop_ro("closed",
                   [](PythonGuiWindowHost &self) {
                     try {
                       return !self.get().is_open();
                     } catch (const std::runtime_error &) {
                       return true;
                     }
                   })
      .def_prop_ro("rendered_frame_count", [](PythonGuiWindowHost &self) {
        return self.get().rendered_frame_count();
      })
      .def_prop_ro("framebuffer_size", [](PythonGuiWindowHost &self) {
        return std::array<int, 2>{self.get().framebuffer_width(),
                                  self.get().framebuffer_height()};
      })
      .def_prop_ro(
          "graphics",
          [](PythonGuiWindowHost &self) -> tgfx::GraphicsHost & {
            return self.get().graphics();
          },
          nb::rv_policy::reference_internal)
      .def("__enter__",
           [](PythonGuiWindowHost &self) -> PythonGuiWindowHost & {
             return self;
           },
           nb::rv_policy::reference_internal)
      .def("__exit__",
           [](PythonGuiWindowHost &self, nb::handle, nb::handle, nb::handle) {
             self.close();
             return false;
           });

  nb::class_<DynamicTextureLease>(m, "DynamicTextureLease")
      .def(
          "__init__",
          [](DynamicTextureLease *self, PythonGuiWindowHost &host) {
            new (self) DynamicTextureLease(host.get());
          },
          nb::arg("host"), nb::keep_alive<1, 2>())
      .def(
          "set_rgba8",
          [](DynamicTextureLease &self, const Rgba8Array &data) {
            const Rgba8View view =
                rgba8_view(data, "DynamicTextureLease.set_rgba8");
            self.set_rgba8(view.width, view.height, view.pixels);
          },
          nb::arg("data"))
      .def(
          "update_region_rgba8",
          [](DynamicTextureLease &self, uint32_t x, uint32_t y,
             const Rgba8Array &data) {
            const Rgba8View view =
                rgba8_view(data, "DynamicTextureLease.update_region_rgba8");
            self.update_region_rgba8(
                x, y, view.width, view.height, view.pixels);
          },
          nb::arg("x"), nb::arg("y"), nb::arg("data"))
      .def(
          "borrow",
          [](DynamicTextureLease &self, tgfx::GraphicsHost &texture_owner,
             tgfx::TextureHandle texture) {
            self.borrow(texture_owner, texture);
          },
          nb::arg("texture_owner"), nb::arg("texture"))
      .def(
          "bind_canvas",
          [](DynamicTextureLease &self, const CanvasRef &canvas,
             CanvasTextureLayer layer) {
            self.bind_canvas(canvas.get(), layer);
          },
          nb::arg("canvas"),
          nb::arg("layer") = CanvasTextureLayer::Image)
      .def(
          "unbind_canvas",
          [](DynamicTextureLease &self, const CanvasRef &canvas,
             CanvasTextureLayer layer) {
            self.unbind_canvas(canvas.get(), layer);
          },
          nb::arg("canvas"),
          nb::arg("layer") = CanvasTextureLayer::Image)
      .def("clear", &DynamicTextureLease::clear)
      .def("close", &DynamicTextureLease::release)
      .def_prop_ro("ownership", &DynamicTextureLease::ownership)
      .def_prop_ro("texture", &DynamicTextureLease::texture)
      .def_prop_ro("width", &DynamicTextureLease::width)
      .def_prop_ro("height", &DynamicTextureLease::height)
      .def_prop_ro("empty", &DynamicTextureLease::empty)
      .def_prop_ro("closed", &DynamicTextureLease::released)
      .def(
          "__enter__",
          [](DynamicTextureLease &self) -> DynamicTextureLease & {
            return self;
          },
          nb::rv_policy::reference_internal)
      .def(
          "__exit__",
          [](DynamicTextureLease &self, nb::handle, nb::handle, nb::handle) {
            self.release();
            return false;
          });

  nb::class_<PythonStandaloneGuiApplication>(m, "StandaloneGuiApplication")
      .def(
          "__init__",
          [](PythonStandaloneGuiApplication *self, const std::string &title,
             int width, int height, tgfx::PresentationMode presentation_mode,
             const std::string &font_path, int font_size,
             std::array<float, 4> clear_color, bool enable_text_input,
             bool continuous_rendering, const std::string &sdk_root,
             const std::string &shader_compiler_path,
             const std::string &slang_compiler_path,
             const std::string &shader_cache_root,
             const std::string &shader_artifact_root,
             bool enable_shader_dev_compile) {
            StandaloneGuiApplicationConfig config;
            config.gui = make_window_config(
                title, width, height, presentation_mode, font_path, font_size,
                clear_color, enable_text_input, continuous_rendering);
            config.sdk_root = sdk_root;
            config.shader_compiler_path = shader_compiler_path;
            config.slang_compiler_path = slang_compiler_path;
            config.shader_cache_root = shader_cache_root;
            config.shader_artifact_root = shader_artifact_root;
            config.enable_shader_dev_compile = enable_shader_dev_compile;
            new (self) PythonStandaloneGuiApplication(std::move(config));
          },
          nb::arg("title") = "Termin", nb::arg("width") = 1280,
          nb::arg("height") = 720,
          nb::arg("presentation_mode") = tgfx::PresentationMode::VSync,
          nb::arg("font_path") = "", nb::arg("font_size") = 14,
          nb::arg("clear_color") =
              std::array<float, 4>{0.03f, 0.035f, 0.045f, 1.0f},
          nb::arg("enable_text_input") = true,
          nb::arg("continuous_rendering") = true, nb::arg("sdk_root") = "",
          nb::arg("shader_compiler_path") = "",
          nb::arg("slang_compiler_path") = "",
          nb::arg("shader_cache_root") = "",
          nb::arg("shader_artifact_root") = "",
          nb::arg("enable_shader_dev_compile") = true)
      .def_prop_ro("document",
                   &PythonStandaloneGuiApplication::document,
                   nb::rv_policy::reference_internal)
      .def_prop_ro("window_host",
                   &PythonStandaloneGuiApplication::window_host,
                   nb::rv_policy::reference_internal)
      .def("tick", [](PythonStandaloneGuiApplication &self) {
        return self.window_host().get().tick();
      })
      .def("close", &PythonStandaloneGuiApplication::close)
      .def_prop_ro("closed",
                   [](const PythonStandaloneGuiApplication &self) {
                     return !self.is_open();
                   })
      .def("__enter__",
           [](PythonStandaloneGuiApplication &self)
               -> PythonStandaloneGuiApplication & { return self; },
           nb::rv_policy::reference_internal)
      .def("__exit__",
           [](PythonStandaloneGuiApplication &self, nb::handle, nb::handle,
              nb::handle) {
             self.close();
             return false;
           });
}
