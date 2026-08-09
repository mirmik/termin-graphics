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

    bool marker_fixture_present(const std::vector<float>& pixels, int width, int height) {
        std::size_t orange = 0;
        std::size_t light_border = 0;
        std::size_t blue_series = 0;
        std::size_t green_series = 0;
        std::size_t magenta_series = 0;
        std::size_t opaque = 0;
        float strongest_orange_r = 0.0f;
        float strongest_orange_g = 0.0f;
        float strongest_orange_b = 0.0f;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
                const float r = pixels[i + 0];
                const float g = pixels[i + 1];
                const float b = pixels[i + 2];
                const float a = pixels[i + 3];
                if (a > 0.9f)
                    ++opaque;
                // Marker colors are authored in sRGB and reach this linear
                // render target after exactly one decode.
                if (r > 0.80f && g > 0.20f && g < 0.35f && b < 0.05f) {
                    ++orange;
                }
                if (r > strongest_orange_r && g > 0.02f && g < 0.40f && b < 0.10f) {
                    strongest_orange_r = r;
                    strongest_orange_g = g;
                    strongest_orange_b = b;
                }
                if (r > 0.52f && g > 0.58f && b > 0.72f) {
                    ++light_border;
                }
                if (b > 0.70f && r < 0.35f && g < 0.65f)
                    ++blue_series;
                if (g > 0.65f && r < 0.35f && b < 0.35f)
                    ++green_series;
                if (r > 0.65f && b > 0.65f && g < 0.35f)
                    ++magenta_series;
            }
        }
        const bool present = opaque > static_cast<std::size_t>(width * height * 9 / 10) && orange >= 20 &&
                             light_border >= 40 && blue_series >= 20 && green_series >= 20 &&
                             magenta_series >= 20;
        if (!present) {
            std::fprintf(stderr,
                         "marker fixture counts: opaque=%zu orange=%zu border=%zu blue=%zu green=%zu magenta=%zu "
                         "strongest_orange=(%.5f, %.5f, %.5f)\n",
                         opaque,
                         orange,
                         light_border,
                         blue_series,
                         green_series,
                         magenta_series,
                         strongest_orange_r,
                         strongest_orange_g,
                         strongest_orange_b);
        }
        return present;
    }

} // namespace

int main() {
    try {
        if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
            std::printf("tcplot marker offscreen smoke skipped: Vulkan unavailable\n");
            return 77;
        }
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        TemporaryShaderRoot shader_root{
            std::filesystem::temp_directory_path() / ("tcplot-marker-smoke-" + std::to_string(unique)),
        };
        std::filesystem::create_directories(shader_root.path / "cache");
        termin::tgfx2_set_shader_artifact_root(shader_root.path.string().c_str());
        termin::tgfx2_set_shader_cache_root((shader_root.path / "cache").string().c_str());
        termin::tgfx2_set_shader_dev_compile_enabled(true);

        tcplot::GpuHost host(TCPLOT_TEST_FONT, tgfx::BackendType::Vulkan);
        tcplot::PlotView2D view(host);
        view.set_msaa_samples(1);
        view.set_view(0.0, 10.0, 0.0, 10.0);
        view.set_title("Retained marker");
        const double series_x[] = {1.0, 3.0, 5.0, 7.0, 9.0};
        const double solid_y[] = {2.0, 3.0, 2.0, 3.0, 2.0};
        const double dashed_y[] = {7.0, 8.0, 7.0, 8.0, 7.0};
        const double colormap_y[] = {8.8, 9.2, 8.8, 9.2, 8.8};
        const double scatter_y[] = {4.0, 6.0, 4.0, 6.0, 4.0};
        const double scalar[] = {0.0, 0.25, 0.5, 0.75, 1.0};
        view.plot({series_x, solid_y, 5},
                  tcplot::LinePlotOptions{{tcplot::SrgbColor{0.1f, 0.3f, 0.9f, 1.0f}}, 2.0, "solid"});
        view.plot({series_x, dashed_y, 5},
                  tcplot::LinePlotOptions{{tcplot::SrgbColor{0.1f, 0.9f, 0.1f, 1.0f}}, 3.0, "dashed"});
        if (!view.set_line_style(1, tcplot::LineStyle::Dash, 10.0f, 5.0f)) {
            std::fprintf(stderr, "failed to style retained line\n");
            return 1;
        }
        view.plot_colormap(
            {series_x, colormap_y, 5},
            scalar,
            tcplot::LineColormapOptions{tcplot::SurfaceColorMap::Viridis, 0.0, 1.0, 3.0, "colormap", true});
        view.scatter({series_x, scatter_y, 5},
                     tcplot::ScatterPlotOptions{{tcplot::SrgbColor{0.9f, 0.1f, 0.9f, 1.0f}}, 10.0, "scatter"});

        tcplot::PlotDataMarker2D marker;
        marker.data_position = {5.0, 5.0};
        marker.text = "sample 5.0";
        const auto marker_handle = view.annotations().create_data_marker(std::move(marker));
        if (!marker_handle) {
            std::fprintf(stderr, "failed to create data marker\n");
            return 1;
        }

        constexpr int width = 320;
        constexpr int height = 240;
        const tgfx::TextureHandle texture = view.render_to_texture(width, height);
        if (texture.id == 0) {
            std::fprintf(stderr, "marker smoke produced no texture\n");
            return 1;
        }
        host.device().wait_idle();
        std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4);
        if (!host.device().read_texture_rgba_float(texture, pixels.data())) {
            std::fprintf(stderr, "marker smoke readback failed\n");
            return 1;
        }
        if (!marker_fixture_present(pixels, width, height)) {
            std::fprintf(stderr, "marker smoke fixture is missing anchor/callout colors\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "tcplot marker offscreen smoke failed: %s\n", error.what());
        return 1;
    }
}
