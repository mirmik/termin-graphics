#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <system_error>
#include <vector>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

#include "tcplot/gpu_host.hpp"
#include "tcplot/plot_annotations2d.hpp"
#include "tcplot/plot_view2d.hpp"

namespace {

struct TemporaryShaderRoot {
    std::filesystem::path path;
    ~TemporaryShaderRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool marker_fixture_present(
    const std::vector<float>& pixels,
    int width,
    int height) {
    std::size_t orange = 0;
    std::size_t light_border = 0;
    std::size_t opaque = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * width + x) * 4;
            const float r = pixels[i + 0];
            const float g = pixels[i + 1];
            const float b = pixels[i + 2];
            const float a = pixels[i + 3];
            if (a > 0.9f) ++opaque;
            if (r > 0.75f && g > 0.30f && g < 0.85f && b < 0.35f) {
                ++orange;
            }
            if (r > 0.68f && g > 0.70f && b > 0.76f) {
                ++light_border;
            }
        }
    }
    return opaque > static_cast<std::size_t>(width * height * 9 / 10)
        && orange >= 20
        && light_border >= 40;
}

}  // namespace

int main() {
    try {
        if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
            std::printf(
                "tcplot marker offscreen smoke skipped: Vulkan unavailable\n");
            return 77;
        }
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        TemporaryShaderRoot shader_root{
            std::filesystem::temp_directory_path()
                / ("tcplot-marker-smoke-" + std::to_string(unique)),
        };
        std::filesystem::create_directories(shader_root.path / "cache");
        termin::tgfx2_set_shader_artifact_root(
            shader_root.path.string().c_str());
        termin::tgfx2_set_shader_cache_root(
            (shader_root.path / "cache").string().c_str());
        termin::tgfx2_set_shader_dev_compile_enabled(true);

        tcplot::GpuHost host(
            TCPLOT_TEST_FONT,
            tgfx::BackendType::Vulkan);
        tcplot::PlotView2D view(host);
        view.set_msaa_samples(1);
        view.set_view(0.0, 10.0, 0.0, 10.0);
        view.set_title("Retained marker");

        tcplot::PlotDataMarker2D marker;
        marker.data_position = {5.0, 5.0};
        marker.text = "sample 5.0";
        const auto marker_handle =
            view.annotations().create_data_marker(std::move(marker));
        if (!marker_handle) {
            std::fprintf(stderr, "failed to create data marker\n");
            return 1;
        }

        constexpr int width = 320;
        constexpr int height = 240;
        const tgfx::TextureHandle texture =
            view.render_to_texture(width, height);
        if (texture.id == 0) {
            std::fprintf(stderr, "marker smoke produced no texture\n");
            return 1;
        }
        host.device().wait_idle();
        std::vector<float> pixels(
            static_cast<std::size_t>(width) * height * 4);
        if (!host.device().read_texture_rgba_float(
                texture, pixels.data())) {
            std::fprintf(stderr, "marker smoke readback failed\n");
            return 1;
        }
        if (!marker_fixture_present(pixels, width, height)) {
            std::fprintf(
                stderr,
                "marker smoke fixture is missing anchor/callout colors\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(
            stderr,
            "tcplot marker offscreen smoke failed: %s\n",
            error.what());
        return 1;
    }
}
