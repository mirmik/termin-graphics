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
#include "tcplot/plot_view2d.hpp"

namespace {

struct TemporaryShaderRoot {
  std::filesystem::path path;
  ~TemporaryShaderRoot() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

} // namespace

int main() {
  try {
    if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
      std::printf("tcplot series performance skipped: Vulkan unavailable\n");
      return 77;
    }
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryShaderRoot shader_root{
        std::filesystem::temp_directory_path() /
        ("tcplot-series-performance-" + std::to_string(unique))};
    std::filesystem::create_directories(shader_root.path / "cache");
    termin::tgfx2_set_shader_artifact_root(shader_root.path.string().c_str());
    termin::tgfx2_set_shader_cache_root(
        (shader_root.path / "cache").string().c_str());
    termin::tgfx2_set_shader_dev_compile_enabled(true);

    tcplot::GpuHost host(TCPLOT_TEST_FONT, tgfx::BackendType::Vulkan);
    tcplot::PlotView2D view(host);
    view.set_msaa_samples(1);
    view.set_view(0.0, 100.0, -1.2, 1.2);

    constexpr std::size_t count = 100000;
    std::vector<double> x(count);
    std::vector<double> line_y(count);
    std::vector<double> scatter_y(count);
    for (std::size_t index = 0; index < count; ++index) {
      x[index] =
          static_cast<double>(index) * 100.0 / static_cast<double>(count - 1);
      line_y[index] = std::sin(x[index] * 0.2);
      scatter_y[index] = std::cos(x[index] * 0.2);
    }
    view.plot({x.data(), line_y.data(), count},
              tcplot::LinePlotOptions{
                  {tcplot::Color4{0.1f, 0.6f, 0.9f, 1.0f}}, 1.5, "100k line"});
    view.scatter(
        {x.data(), scatter_y.data(), count},
        tcplot::ScatterPlotOptions{
            {tcplot::Color4{0.9f, 0.3f, 0.2f, 0.7f}}, 3.0, "100k scatter"});

    constexpr int width = 1280;
    constexpr int height = 720;
    if (!view.render_to_texture(width, height))
      return 1;
    host.device().wait_idle();

    const auto start = std::chrono::steady_clock::now();
    constexpr int measured_frames = 5;
    for (int frame = 0; frame < measured_frames; ++frame) {
      if (!view.render_to_texture(width, height))
        return 1;
    }
    host.device().wait_idle();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double milliseconds =
        std::chrono::duration<double, std::milli>(elapsed).count();
    const double per_frame = milliseconds / measured_frames;
    std::printf("retained-series steady render: line=100000 scatter=100000 "
                "1280x720, %.3f ms/frame\n",
                per_frame);

    // This gate intentionally includes texture allocation and GPU wait.
    // The pre-migration scatter path emitted 100k Canvas circles per
    // frame; the retained path must remain comfortably interactive.
    return per_frame <= 250.0 ? 0 : 1;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "tcplot series performance failed: %s\n",
                 error.what());
    return 1;
  }
}
