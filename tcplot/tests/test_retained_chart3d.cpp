#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

#include "tcplot/gpu_host.hpp"
#include "tcplot/retained_chart3d.h"

namespace {

struct TemporaryShaderRoot {
    std::filesystem::path path;

    ~TemporaryShaderRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

tc_plot_item3d_snapshot snapshot(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle item) {
    tc_plot_item3d_snapshot result{};
    require(
        tc_retained_chart3d_item_snapshot(chart, item, &result) != 0,
        "failed to snapshot retained item");
    return result;
}

bool same_snapshot(
    const tc_plot_item3d_snapshot& left,
    const tc_plot_item3d_snapshot& right) {
    return left.kind == right.kind &&
           left.geometry_revision == right.geometry_revision &&
           left.style_revision == right.style_revision &&
           left.gpu_revision == right.gpu_revision;
}

}  // namespace

int main() {
    try {
        if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
            std::printf(
                "retained Chart3D test skipped: Vulkan unavailable\n");
            return 77;
        }

        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        TemporaryShaderRoot shader_root{
            std::filesystem::temp_directory_path() /
            ("tcplot-retained-chart3d-" + std::to_string(unique)),
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
        tc_retained_chart3d* chart = tc_retained_chart3d_create(&host);
        tc_retained_chart3d* other = tc_retained_chart3d_create(&host);
        require(chart != nullptr && other != nullptr, "failed to create charts");
        require(
            tc_retained_chart3d_scene_id(chart) !=
                tc_retained_chart3d_scene_id(other),
            "chart scene ids must be unique");
        require(
            tc_retained_chart3d_item_count(chart) == 1,
            "chart must create one default grid part");

        const double x[] = {-1.0, 1.0, -1.0, 1.0};
        const double y[] = {-1.0, -1.0, 1.0, 1.0};
        const double z[] = {0.0, 0.5, 1.0, 0.25};
        tc_surface_item3d_style surface_style{
            1.0f, 1.0f, 1.0f, 1.0f,
            TC_PLOT_COLORMAP3D_VIRIDIS,
            0, 0, 1, 1, 1, 1.0f,
        };
        const tc_plot_item3d_handle surface =
            tc_retained_chart3d_add_surface(
                chart, x, y, z, 2, 2, &surface_style);
        require(
            tc_retained_chart3d_item_is_valid(chart, surface) != 0,
            "surface handle must be valid in its scene");
        require(
            tc_retained_chart3d_item_is_valid(other, surface) == 0,
            "cross-scene surface handle must be rejected");
        require(
            tc_retained_chart3d_surface_set_style(
                other, surface, &surface_style) == 0,
            "cross-scene surface mutation must be rejected");

        const double scatter_x[] = {-0.5, 0.0, 0.75};
        const double scatter_y[] = {0.5, -0.25, 0.25};
        const double scatter_z[] = {0.25, 0.75, 0.5};
        tc_scatter_item3d_style scatter_style{
            1.0f, 0.25f, 0.1f, 1.0f, 5.0f,
        };
        const tc_plot_item3d_handle scatter =
            tc_retained_chart3d_add_scatter(
                chart,
                scatter_x, scatter_y, scatter_z,
                3, &scatter_style);

        const auto surface_initial = snapshot(chart, surface);
        const auto scatter_initial = snapshot(chart, scatter);
        require(
            tc_retained_chart3d_surface_set_style(
                chart, surface, &surface_style) != 0,
            "identical surface style must be accepted");
        require(
            same_snapshot(surface_initial, snapshot(chart, surface)),
            "identical style must be a no-op");

        tc_orbit_camera3d_state camera{};
        require(
            tc_retained_chart3d_get_camera(chart, &camera) != 0,
            "failed to read camera");
        camera.azimuth += 0.25f;
        require(
            tc_retained_chart3d_set_camera(chart, &camera) != 0,
            "failed to update camera");
        require(
            tc_retained_chart3d_set_surface_shading(chart, 1, 0.4f) != 0 &&
                tc_retained_chart3d_set_surface_shading(
                    chart,
                    1,
                    std::numeric_limits<float>::quiet_NaN()) == 0,
            "shading validation must be observable by callers");
        require(
            tc_retained_chart3d_set_light_direction(chart, 0, 0, 0) == 0 &&
                tc_retained_chart3d_set_axis_scale(chart, 1, -1, 1) == 0,
            "invalid chart policy values must be rejected");
        require(
            same_snapshot(surface_initial, snapshot(chart, surface)) &&
                same_snapshot(scatter_initial, snapshot(chart, scatter)),
            "camera changes must not invalidate item state");

        require(
            tc_retained_chart3d_render(chart, 320, 240) != 0,
            "initial retained render failed");
        const auto surface_rendered = snapshot(chart, surface);
        const auto scatter_rendered = snapshot(chart, scatter);
        require(
            surface_rendered.gpu_revision != 0 &&
                scatter_rendered.gpu_revision != 0,
            "render must synchronize item GPU revisions");

        surface_style.wireframe = 1;
        require(
            tc_retained_chart3d_surface_set_style(
                chart, surface, &surface_style) != 0,
            "failed to update surface style");
        const auto surface_invalidated = snapshot(chart, surface);
        require(
            surface_invalidated.geometry_revision ==
                surface_rendered.geometry_revision &&
                surface_invalidated.style_revision ==
                    surface_rendered.style_revision + 1 &&
                surface_invalidated.gpu_revision == 0,
            "style change must preserve semantic geometry and invalidate GPU state");
        require(
            same_snapshot(scatter_rendered, snapshot(chart, scatter)),
            "surface style must not invalidate unrelated scatter");

        tc_surface_item3d_style invalid_style = surface_style;
        invalid_style.surface_grid_width_px = -1.0f;
        require(
            tc_retained_chart3d_surface_set_style(
                chart, surface, &invalid_style) == 0,
            "invalid surface style must be rejected");
        require(
            same_snapshot(surface_invalidated, snapshot(chart, surface)),
            "rejected style must not mutate item revisions");

        require(
            tc_retained_chart3d_render(chart, 512, 256) != 0,
            "resized retained render failed");
        require(
            snapshot(chart, surface).gpu_revision != 0,
            "style update must be synchronized by render");
        require(
            same_snapshot(scatter_rendered, snapshot(chart, scatter)),
            "resizing must not rebuild unrelated item geometry");

        tc_retained_chart3d_release_gpu(chart);
        require(
            snapshot(chart, surface).gpu_revision == 0 &&
                snapshot(chart, scatter).gpu_revision == 0,
            "GPU release must invalidate item GPU revisions");
        require(
            tc_retained_chart3d_render(chart, 512, 256) != 0,
            "render after GPU release failed");
        require(
            snapshot(chart, surface).gpu_revision != 0 &&
                snapshot(chart, scatter).gpu_revision != 0,
            "render after GPU release must rebuild item resources");

        require(
            tc_retained_chart3d_destroy_item(chart, scatter) != 0,
            "failed to destroy scatter");
        require(
            tc_retained_chart3d_item_is_valid(chart, scatter) == 0,
            "destroyed handle must be stale");
        const tc_plot_item3d_handle replacement =
            tc_retained_chart3d_add_scatter(
                chart,
                scatter_x, scatter_y, scatter_z,
                3, &scatter_style);
        require(
            replacement.index == scatter.index &&
                replacement.generation != scatter.generation,
            "reused slot must advance its generation");
        require(
            tc_retained_chart3d_destroy_item(chart, scatter) == 0,
            "stale handle must not destroy replacement item");

        tc_retained_chart3d_destroy(other);
        tc_retained_chart3d_destroy(chart);
        std::printf("retained Chart3D lifecycle and invalidation test passed\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(
            stderr,
            "retained Chart3D test failed: %s\n",
            error.what());
        return 1;
    }
}
