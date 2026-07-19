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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/application_host.hpp>
#include <termin/gui_native/showcase.hpp>
#include <termin/gui_native/widgets.hpp>

#include <tgfx2/i_render_device.hpp>

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

bool write_ppm_screenshot(tgfx::IRenderDevice& device, tgfx::TextureHandle target, int width,
                          int height, const std::filesystem::path& path) {
    std::vector<float> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    device.wait_idle();
    if (!device.read_texture_rgba_float(target, pixels.data())) {
        std::fprintf(stderr, "termin-gui-native example: screenshot readback failed\n");
        return false;
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        std::fprintf(stderr, "termin-gui-native example: cannot open screenshot path %s\n",
                     path.string().c_str());
        return false;
    }
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (size_t index = 0; index < pixels.size(); index += 4u) {
        const unsigned char rgb[3] = {
            static_cast<unsigned char>(std::lround(std::clamp(pixels[index], 0.0f, 1.0f) * 255.0f)),
            static_cast<unsigned char>(
                std::lround(std::clamp(pixels[index + 1], 0.0f, 1.0f) * 255.0f)),
            static_cast<unsigned char>(
                std::lround(std::clamp(pixels[index + 2], 0.0f, 1.0f) * 255.0f)),
        };
        output.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    }
    if (!output) {
        std::fprintf(stderr, "termin-gui-native example: screenshot write failed for %s\n",
                     path.string().c_str());
        return false;
    }
    std::printf("termin-gui-native screenshot: %s\n", path.string().c_str());
    return true;
}

} // namespace

int run_showcase_window(const char* title) {
    try {
        Document document;
        ShowcaseRefs showcase = build_showcase(document);
        ApplicationHostConfig host_config;
        host_config.window = WindowConfig{
            title ? title : "termin-gui-native showcase", 800, 600};
        host_config.font_size = 15;
        ApplicationHost host(document, std::move(host_config));

        const double max_seconds = example_seconds();
        const std::filesystem::path capture_path = screenshot_path();
        const auto start = std::chrono::steady_clock::now();

        while (!host.should_close()) {
            host.pump_events();
            if (host.should_close()) {
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            const double elapsed =
                capture_path.empty() ? std::chrono::duration<double>(now - start).count() : 0.0;
            if (showcase.progress) {
                showcase.progress->set_value(static_cast<float>((std::sin(elapsed) + 1.0) * 0.5));
            }
            if (showcase.slider && showcase.checkbox && showcase.checkbox->checked()) {
                showcase.slider->set_value(
                    static_cast<float>((std::sin(elapsed * 0.6) + 1.0) * 0.5));
            }
            if (!host.render_frame()) {
                continue;
            }
            if (!capture_path.empty() &&
                !write_ppm_screenshot(
                    *host.window().device(),
                    host.color_target(),
                    host.framebuffer_width(),
                    host.framebuffer_height(),
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

        host.wait_idle();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "termin-gui-native showcase example failed: %s\n", e.what());
        if (std::strstr(e.what(), "No available video device") ||
            std::strstr(e.what(), "Vulkan support is either not configured in SDL")) {
            return 77;
        }
        return 1;
    }
}

} // namespace termin::gui_native::examples
