#include "showcase_window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/showcase.hpp>
#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/tc_ui_document.h>
#include <termin/gui_native/widgets.hpp>
#include <termin/gui_native/window_adapter.hpp>
#include <termin/platform/backend_window.hpp>
#include <termin/window/window_manager.hpp>

#include <tgfx2/i_render_device.hpp>
#include <tgfx2/standalone_shader_runtime.hpp>

namespace termin::gui_native::examples {
    namespace {

        double example_seconds() {
            const char* value = std::getenv("TERMIN_GUI_NATIVE_EXAMPLE_SECONDS");
            if (!value || value[0] == '\0') {
                return 0.0;
            }
            char* end = nullptr;
            const double parsed = std::strtod(value, &end);
            if (end == value || parsed < 0.0) {
                return 0.0;
            }
            return parsed;
        }

        std::filesystem::path screenshot_path() {
            const char* value = std::getenv("TERMIN_GUI_NATIVE_SCREENSHOT");
            return value && value[0] != '\0' ? std::filesystem::path(value) : std::filesystem::path{};
        }

        std::string resolve_font_path() {
            if (const char* configured = std::getenv("TERMIN_UI_FONT"); configured && configured[0]) {
                return configured;
            }
            const std::filesystem::path sdk_root =
                std::getenv("TERMIN_SDK") ? std::getenv("TERMIN_SDK") : std::filesystem::path{};
            const std::filesystem::path candidates[] = {
                sdk_root / "share" / "termin" / "fonts" / "DroidSans.ttf",
                std::filesystem::path("sdk/share/termin/fonts/DroidSans.ttf"),
                std::filesystem::path("/usr/local/share/termin/fonts/DroidSans.ttf"),
            };
            for (const auto& candidate : candidates) {
                if (!candidate.empty() && std::filesystem::is_regular_file(candidate)) {
                    return candidate.string();
                }
            }
            throw std::runtime_error("cannot find UI font; set TERMIN_UI_FONT or TERMIN_SDK");
        }

        bool write_ppm_screenshot(tgfx::IRenderDevice& device,
                                  tgfx::TextureHandle target,
                                  int width,
                                  int height,
                                  const std::filesystem::path& path) {
            std::vector<float> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
            device.wait_idle();
            if (!device.read_texture_rgba_float(target, pixels.data())) {
                std::fprintf(stderr, "termin-gui-native example: screenshot readback failed\n");
                return false;
            }
            std::ofstream output(path, std::ios::binary);
            if (!output) {
                std::fprintf(
                    stderr, "termin-gui-native example: cannot open screenshot path %s\n", path.string().c_str());
                return false;
            }
            output << "P6\n" << width << ' ' << height << "\n255\n";
            for (size_t index = 0; index < pixels.size(); index += 4u) {
                const unsigned char rgb[3] = {
                    static_cast<unsigned char>(std::lround(std::clamp(pixels[index], 0.0f, 1.0f) * 255.0f)),
                    static_cast<unsigned char>(std::lround(std::clamp(pixels[index + 1], 0.0f, 1.0f) * 255.0f)),
                    static_cast<unsigned char>(std::lround(std::clamp(pixels[index + 2], 0.0f, 1.0f) * 255.0f)),
                };
                output.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
            }
            if (!output) {
                std::fprintf(
                    stderr, "termin-gui-native example: screenshot write failed for %s\n", path.string().c_str());
                return false;
            }
            std::printf("termin-gui-native screenshot: %s\n", path.string().c_str());
            return true;
        }

    } // namespace

    int run_document_window(const char* title, DocumentBuildCallback build, ExampleTickCallback tick) {
        try {
            if (!build) {
                throw std::invalid_argument("termin-gui-native example requires a document builder");
            }
            auto session = create_native_windowed_graphics();
            if (!tgfx::configure_default_standalone_shader_runtime(session->graphics(), "gui-native-window-examples")) {
                throw std::runtime_error("standalone shader runtime configuration failed");
            }
            WindowManager windows(*session);
            const WindowHandle handle =
                windows.create_window(WindowConfig{title ? title : "termin-gui-native showcase", 800, 600});
            const tc_ui_document_handle document_handle = tc_ui_document_create();
            TcDocument document(document_handle);
            DocumentRendererConfig renderer_config;
            renderer_config.font_path = resolve_font_path();
            renderer_config.font_size = 15;
            GuiWindowAdapter adapter(session->graphics(), document, renderer_config, windows.window(handle));
            build(document);

            const double max_seconds = example_seconds();
            const std::filesystem::path capture_path = screenshot_path();
            const auto start = std::chrono::steady_clock::now();

            while (!windows.window(handle).should_close()) {
                windows.pump_events();
                adapter.consume_events(windows.take_events(handle));
                if (windows.window(handle).should_close()) {
                    break;
                }
                const auto now = std::chrono::steady_clock::now();
                const double elapsed = capture_path.empty() ? std::chrono::duration<double>(now - start).count() : 0.0;
                if (tick)
                    tick(elapsed);
                if (!adapter.render_and_present()) {
                    continue;
                }
                if (!capture_path.empty() && !write_ppm_screenshot(adapter.renderer().device(),
                                                                   adapter.renderer().color_target(),
                                                                   adapter.renderer().framebuffer_size().first,
                                                                   adapter.renderer().framebuffer_size().second,
                                                                   capture_path)) {
                    throw std::runtime_error("failed to capture deterministic native UI screenshot");
                }

                if (!capture_path.empty()) {
                    break;
                }

                if (max_seconds > 0.0) {
                    const double elapsed = std::chrono::duration<double>(now - start).count();
                    if (elapsed >= max_seconds) {
                        break;
                    }
                }
            }

            adapter.wait_idle();
            adapter.close();
            tc_ui_document_destroy(document_handle);
            windows.close();
            session->close();
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "termin-gui-native window example failed: %s\n", e.what());
            if (std::strstr(e.what(), "No available video device") ||
                std::strstr(e.what(), "Vulkan support is either not configured in SDL")) {
                return 77;
            }
            return 1;
        }
    }

    int run_showcase_window(const char* title) {
        auto showcase = std::make_shared<ShowcaseRefs>();
        return run_document_window(
            title,
            [showcase](TcDocument document) { *showcase = build_showcase(document); },
            [showcase](double elapsed) {
                if (showcase->progress) {
                    showcase->progress->set_value(static_cast<float>((std::sin(elapsed) + 1.0) * 0.5));
                }
                if (showcase->slider && showcase->checkbox && showcase->checkbox->checked()) {
                    showcase->slider->set_value(static_cast<float>((std::sin(elapsed * 0.6) + 1.0) * 0.5));
                }
            });
    }

} // namespace termin::gui_native::examples
