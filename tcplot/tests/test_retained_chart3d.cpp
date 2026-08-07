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

#include <termin/render/builtin_passes.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/render/render_pipeline.hpp>

#include "tcplot/gpu_host.hpp"
#include "tcplot/plot_scene3d_render_item_source.hpp"
#include "tcplot/retained_chart3d.h"

extern "C" {
#include <render/tc_pass.h>
}

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

const tc_render_item* find_render_item(
    const termin::RenderItemSnapshot& snapshot,
    tc_plot_item3d_handle handle) {
    for (const tc_render_item& item : snapshot.items()) {
        if (item.source.namespace_id == handle.scene_id &&
            item.source.object_id == handle.index &&
            item.source.generation == handle.generation) {
            return &item;
        }
    }
    return nullptr;
}

const tcplot::PlotScene3DRenderItemPayload* find_plot_payload(
    const termin::RenderItemSnapshot& snapshot,
    tc_plot_item3d_handle handle) {
    const tc_render_item* item = find_render_item(snapshot, handle);
    return item ? tcplot::plot_scene3d_render_item_payload(*item) : nullptr;
}

constexpr const char* kPlotSnapshotProbeType = "PlotScene3DSnapshotProbe";
const termin::RenderItemSnapshot* g_probe_snapshot = nullptr;
std::size_t g_probe_item_count = 0;
bool g_probe_executed = false;

class PlotSnapshotProbe final : public termin::CxxFramePass {
public:
    PlotSnapshotProbe() {
        pass_name_set(kPlotSnapshotProbeType);
        link_to_type_registry(kPlotSnapshotProbeType);
    }

    void execute(termin::ExecuteContext& context) override {
        g_probe_snapshot = context.render_item_snapshot;
        g_probe_item_count = context.render_item_snapshot
            ? context.render_item_snapshot->item_count()
            : 0;
        g_probe_executed = true;
    }
};

void execute_snapshot_probe(
    const termin::RenderItemSnapshot& snapshot,
    tcplot::GpuHost& host) {
    if (!tc_pass_registry_has("CxxFramePass")) {
        termin::register_builtin_render_pass_types();
    }
    tc_pass_registry_unregister(kPlotSnapshotProbeType);
    auto descriptor =
        termin::FramePassTypeDescriptorBuilder::native<PlotSnapshotProbe>(
            kPlotSnapshotProbeType,
            "tcplot-test");
    require(descriptor.commit(), "failed to register plot snapshot probe");

    termin::RenderPipeline pipeline("plot-scene3d-source-test");
    require(pipeline.is_valid(), "failed to create plot snapshot probe pipeline");
    tc_pass* pass = tc_pass_registry_create(kPlotSnapshotProbeType);
    require(pass != nullptr, "failed to create plot snapshot probe pass");
    pipeline.add_pass(pass);

    termin::RenderTargetContext target;
    target.name = "PlotScene3DTarget";
    target.render_rect = {0, 0, 1, 1};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name, termin::RenderExecutionTarget{
        .context = &target,
        .render_items = &snapshot,
    });

    g_probe_snapshot = nullptr;
    g_probe_item_count = 0;
    g_probe_executed = false;
    termin::RenderEngine engine;
    engine.set_graphics_host(host.graphics());
    engine.execute_pipeline(execution);
    require(g_probe_executed, "generic render pipeline did not execute plot probe");
    require(g_probe_snapshot == &snapshot, "plot probe received another snapshot");
    require(
        g_probe_item_count == snapshot.item_count(),
        "plot probe observed the wrong item count");

    pipeline.destroy();
    tc_pass_registry_unregister(kPlotSnapshotProbeType);
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

        termin::RenderItemSnapshot empty_snapshot;
        const tc_plot_item3d_handle other_grid =
            tc_retained_chart3d_grid_part(other);
        require(
            tc_retained_chart3d_destroy_item(other, other_grid) != 0,
            "failed to remove the other chart default grid");
        termin::RenderItemSource& empty_source =
            tcplot::plot_scene3d_render_item_source(*other);
        require(
            empty_source.publish(empty_snapshot, {.debug_name = "EmptyPlotScene3D"}),
            "empty PlotScene3D source publication failed");
        require(
            empty_snapshot.valid() && empty_snapshot.item_count() == 0 &&
                empty_snapshot.counters().source_traversals == 1 &&
                empty_snapshot.counters().producers == 0,
            "empty PlotScene3D snapshot counters are invalid");

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

        termin::RenderItemSource& render_item_source =
            tcplot::plot_scene3d_render_item_source(*chart);
        termin::RenderViewState first_view;
        termin::RenderViewState second_view;
        termin::RenderItemSnapshot first_render_snapshot;
        termin::RenderItemSnapshot second_render_snapshot;
        require(
            render_item_source.publish(
                first_render_snapshot,
                {.view = &first_view, .debug_name = "PlotScene3D first view"}),
            "first PlotScene3D source publication failed");
        require(
            render_item_source.publish(
                second_render_snapshot,
                {.view = &second_view, .debug_name = "PlotScene3D second view"}),
            "second PlotScene3D source publication failed");
        require(
            first_render_snapshot.valid() && second_render_snapshot.valid() &&
                first_render_snapshot.item_count() == 3 &&
                second_render_snapshot.item_count() == 3,
            "multi-view PlotScene3D snapshots must remain independently valid");
        require(
            first_render_snapshot.counters().source_traversals == 1 &&
                first_render_snapshot.counters().producers == 3,
            "PlotScene3D snapshot counters are invalid");

        const tc_render_item* surface_render_item =
            find_render_item(first_render_snapshot, surface);
        const tc_render_item* scatter_render_item =
            find_render_item(first_render_snapshot, scatter);
        const tc_render_item* grid_render_item = find_render_item(
            first_render_snapshot,
            tc_retained_chart3d_grid_part(chart));
        require(
            surface_render_item && scatter_render_item && grid_render_item,
            "PlotScene3D snapshot lost retained item identity");
        require(
            surface_render_item->kind == tcplot::PLOT_RENDER_ITEM_KIND_SURFACE &&
                scatter_render_item->kind == tcplot::PLOT_RENDER_ITEM_KIND_SCATTER &&
                grid_render_item->kind == tcplot::PLOT_RENDER_ITEM_KIND_GRID,
            "PlotScene3D snapshot published incorrect item kinds");
        for (const tc_render_item& item : first_render_snapshot.items()) {
            require(
                item.source.domain_id == tcplot::PLOT_RENDER_ITEM_SOURCE_DOMAIN &&
                    item.source.namespace_id ==
                        tc_retained_chart3d_scene_id(chart) &&
                    item.source.adapter_data != 0 &&
                    tcplot::plot_scene3d_render_item_payload(item) != nullptr,
                "PlotScene3D item must retain an immutable adapter payload");
        }
        const tcplot::PlotScene3DRenderItemPayload* first_surface_payload =
            find_plot_payload(first_render_snapshot, surface);
        const tcplot::PlotScene3DRenderItemPayload* first_scatter_payload =
            find_plot_payload(first_render_snapshot, scatter);
        require(
            first_surface_payload && first_surface_payload->item &&
                first_scatter_payload && first_scatter_payload->item,
            "PlotScene3D payload lookup failed");
        require(
            first_surface_payload->item->z ==
                std::vector<double>(std::begin(z), std::end(z)) &&
                first_surface_payload->item->surface_style.wireframe == 0 &&
                first_surface_payload->frame.x_label == "x",
            "PlotScene3D payload lost item or chart values");
        const tcplot::PlotScene3DRenderItemPayload* second_surface_payload =
            find_plot_payload(second_render_snapshot, surface);
        require(
            second_surface_payload && second_surface_payload->item ==
                first_surface_payload->item,
            "unchanged item data must be shared between snapshots");
        execute_snapshot_probe(first_render_snapshot, host);

        const double changed_z[] = {0.25, 0.75, 1.25, 0.5};
        require(
            tc_retained_chart3d_surface_set_data(
                chart, surface, x, y, changed_z, 2, 2) != 0,
            "failed to update surface data for snapshot isolation");
        termin::RenderItemSnapshot geometry_changed_snapshot;
        require(
            render_item_source.publish(geometry_changed_snapshot, {}),
            "PlotScene3D publication after geometry mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* changed_surface_payload =
            find_plot_payload(geometry_changed_snapshot, surface);
        require(
            changed_surface_payload && changed_surface_payload->item &&
                changed_surface_payload->item->z ==
                    std::vector<double>(
                        std::begin(changed_z), std::end(changed_z)) &&
                changed_surface_payload->geometry_revision ==
                    first_surface_payload->geometry_revision + 1 &&
                changed_surface_payload->item != first_surface_payload->item,
            "geometry mutation must publish a new immutable item payload");
        require(
            first_surface_payload->item->z ==
                std::vector<double>(std::begin(z), std::end(z)) &&
                find_plot_payload(geometry_changed_snapshot, scatter)->item ==
                    first_scatter_payload->item,
            "geometry mutation changed an older or unrelated snapshot payload");

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
        termin::RenderItemSnapshot chart_state_snapshot;
        require(
            render_item_source.publish(chart_state_snapshot, {}),
            "PlotScene3D publication after chart-state mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* chart_state_payload =
            find_plot_payload(chart_state_snapshot, surface);
        require(
            chart_state_payload &&
                chart_state_payload->frame.camera.azimuth == camera.azimuth &&
                chart_state_payload->frame.surface_shading_strength == 0.4f &&
                first_surface_payload->frame.camera.azimuth != camera.azimuth,
            "chart-state mutation must publish values without altering older snapshots");

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
        termin::RenderItemSnapshot style_changed_snapshot;
        require(
            render_item_source.publish(style_changed_snapshot, {}),
            "PlotScene3D publication after style mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* styled_surface_payload =
            find_plot_payload(style_changed_snapshot, surface);
        require(
            styled_surface_payload && styled_surface_payload->item &&
                styled_surface_payload->item->surface_style.wireframe == 1 &&
                first_surface_payload->item->surface_style.wireframe == 0 &&
                styled_surface_payload->style_revision ==
                    first_surface_payload->style_revision + 1,
            "style mutation must not alter an older snapshot payload");
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
        termin::RenderItemSnapshot after_destroy_snapshot;
        require(
            render_item_source.publish(after_destroy_snapshot, {}),
            "PlotScene3D publication after destroy failed");
        require(
            find_render_item(after_destroy_snapshot, scatter) == nullptr,
            "destroyed item must disappear from PlotScene3D snapshots");
        const tc_plot_item3d_handle replacement =
            tc_retained_chart3d_add_scatter(
                chart,
                scatter_x, scatter_y, scatter_z,
                3, &scatter_style);
        require(
            replacement.index == scatter.index &&
                replacement.generation != scatter.generation,
            "reused slot must advance its generation");
        termin::RenderItemSnapshot replacement_snapshot;
        require(
            render_item_source.publish(replacement_snapshot, {}),
            "PlotScene3D publication after slot reuse failed");
        require(
            find_render_item(replacement_snapshot, scatter) == nullptr &&
                find_render_item(replacement_snapshot, replacement) != nullptr,
            "PlotScene3D snapshot must publish only the live slot generation");
        const tcplot::PlotScene3DRenderItemPayload* replacement_payload =
            find_plot_payload(replacement_snapshot, replacement);
        require(
            replacement_payload && replacement_payload->item &&
                replacement_payload->item != first_scatter_payload->item,
            "reused slot must not recycle the previous generation's payload");
        require(
            tc_retained_chart3d_destroy_item(chart, scatter) == 0,
            "stale handle must not destroy replacement item");

        tc_retained_chart3d_destroy(other);
        tc_retained_chart3d_destroy(chart);
        require(
            first_surface_payload->item->z ==
                std::vector<double>(std::begin(z), std::end(z)) &&
                first_surface_payload->frame.x_label == "x" &&
                first_surface_payload->item->surface_style.wireframe == 0,
            "snapshot-owned PlotScene3D payload did not survive chart destruction");
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
