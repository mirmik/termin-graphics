#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#include <tgfx2/builtin_shader_sources.hpp>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

#include <termin/geom/color.hpp>

#include <termin/render/builtin_passes.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/render/render_item_submission.hpp>
#include <termin/render/render_pipeline.hpp>
#include <termin/render/render_task.hpp>

#include "tcplot/gpu_host.hpp"
#include "tcplot/plot_scene3d_render_item_source.hpp"
#include "tcplot/retained_chart3d.h"

#include "../src/plot_scene3d_chart_chrome.hpp"

extern "C" {
#include <render/tc_pass.h>
#include <tgfx/resources/tc_shader_registry.h>
}

namespace {

    void require(bool condition, const char* message);

    void test_termin_clip_canvas_projection() {
        const auto top_left = tcplot::detail::termin_clip_ndc_to_canvas(-1.0f, -1.0f, 320, 240);
        const auto center = tcplot::detail::termin_clip_ndc_to_canvas(0.0f, 0.0f, 320, 240);
        const auto bottom_right = tcplot::detail::termin_clip_ndc_to_canvas(1.0f, 1.0f, 320, 240);

        require(top_left.x == 0.0f && top_left.y == 0.0f,
                "TerminClip top-left must project to canvas top-left");
        require(center.x == 160.0f && center.y == 120.0f,
                "TerminClip center must project to canvas center");
        require(bottom_right.x == 320.0f && bottom_right.y == 240.0f,
                "TerminClip bottom-right must project to canvas bottom-right");
    }

    struct TemporaryShaderRoot {
        std::filesystem::path path;

        ~TemporaryShaderRoot() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    void require(bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    tc_plot_item3d_snapshot snapshot(tc_retained_chart3d* chart, tc_plot_item3d_handle item) {
        tc_plot_item3d_snapshot result{};
        require(tc_retained_chart3d_item_snapshot(chart, item, &result) != 0, "failed to snapshot retained item");
        return result;
    }

    bool same_snapshot(const tc_plot_item3d_snapshot& left, const tc_plot_item3d_snapshot& right) {
        return left.kind == right.kind && left.geometry_revision == right.geometry_revision &&
               left.style_revision == right.style_revision && left.gpu_revision == right.gpu_revision;
    }

    std::vector<float> read_pixels(tcplot::GpuHost& host, uint32_t texture_id, uint32_t width, uint32_t height) {
        tgfx::TextureHandle texture{};
        texture.id = texture_id;
        std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4u, 0.0f);
        require(host.device().read_texture_rgba_float(texture, pixels.data()),
                "failed to read retained Chart3D encoder output");
        return pixels;
    }

    std::size_t count_non_clear_pixels(tcplot::GpuHost& host, uint32_t texture_id, uint32_t width, uint32_t height) {
        const std::vector<float> pixels = read_pixels(host, texture_id, width, height);
        const termin::LinearColor clear =
            termin::srgb_to_linear(termin::SrgbColor{0.08f, 0.09f, 0.11f, 1.0f});
        std::size_t count = 0;
        for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
            if (std::abs(pixels[index + 0] - clear.r) > 0.03f ||
                std::abs(pixels[index + 1] - clear.g) > 0.03f ||
                std::abs(pixels[index + 2] - clear.b) > 0.03f) {
                ++count;
            }
        }
        return count;
    }

    std::size_t count_changed_pixels(const std::vector<float>& left, const std::vector<float>& right) {
        require(left.size() == right.size(), "cannot compare retained Chart3D images with different sizes");
        std::size_t count = 0;
        for (std::size_t index = 0; index + 3 < left.size(); index += 4) {
            if (std::abs(left[index + 0] - right[index + 0]) > 0.01f ||
                std::abs(left[index + 1] - right[index + 1]) > 0.01f ||
                std::abs(left[index + 2] - right[index + 2]) > 0.01f) {
                ++count;
            }
        }
        return count;
    }

    const tc_render_item* find_render_item(const termin::RenderItemSnapshot& snapshot, tc_plot_item3d_handle handle) {
        for (const tc_render_item& item : snapshot.items()) {
            if (item.source.namespace_id == handle.scene_id && item.source.object_id == handle.index &&
                item.source.generation == handle.generation) {
                return &item;
            }
        }
        return nullptr;
    }

    const tcplot::PlotScene3DRenderItemPayload* find_plot_payload(const termin::RenderItemSnapshot& snapshot,
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
            g_probe_item_count = context.render_item_snapshot ? context.render_item_snapshot->item_count() : 0;
            g_probe_executed = true;
        }
    };

    void execute_snapshot_probe(const termin::RenderItemSnapshot& snapshot, tcplot::GpuHost& host) {
        if (!tc_pass_registry_has("CxxFramePass")) {
            termin::register_builtin_render_pass_types();
        }
        tc_pass_registry_unregister(kPlotSnapshotProbeType);
        auto descriptor =
            termin::FramePassTypeDescriptorBuilder::native<PlotSnapshotProbe>(kPlotSnapshotProbeType, "tcplot-test");
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
        execution.targets.emplace(target.name,
                                  termin::RenderExecutionTarget{
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
        require(g_probe_item_count == snapshot.item_count(), "plot probe observed the wrong item count");

        pipeline.destroy();
        tc_pass_registry_unregister(kPlotSnapshotProbeType);
    }

} // namespace

int main() {
    try {
        test_termin_clip_canvas_projection();

        if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
            std::printf("retained Chart3D test skipped: Vulkan unavailable\n");
            return 77;
        }

        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        TemporaryShaderRoot shader_root{
            std::filesystem::temp_directory_path() / ("tcplot-retained-chart3d-" + std::to_string(unique)),
        };
        std::filesystem::create_directories(shader_root.path / "cache");
        termin::tgfx2_set_shader_artifact_root(shader_root.path.string().c_str());
        termin::tgfx2_set_shader_cache_root((shader_root.path / "cache").string().c_str());
        termin::tgfx2_set_shader_dev_compile_enabled(true);

        tcplot::GpuHost host(TCPLOT_TEST_FONT, tgfx::BackendType::Vulkan);
        tc_retained_chart3d* detached = tc_retained_chart3d_create(nullptr);
        require(detached != nullptr, "failed to create detached chart");
        const double line_x[] = {-1.0, 0.0, 1.0};
        const double line_y[] = {0.0, 1.0, 0.0};
        const double line_z[] = {0.0, 0.5, 1.0};
        tc_line_item3d_style line_style{
            0.2f,
            0.6f,
            1.0f,
            1.0f,
            2.0f,
        };
        const tc_plot_item3d_handle line =
            tc_retained_chart3d_add_line(detached, line_x, line_y, line_z, 3, &line_style);
        require(tc_retained_chart3d_item_is_valid(detached, line) != 0,
                "detached chart must accept CPU line data before GPU attachment");
        termin::RenderItemSnapshot detached_snapshot;
        require(tcplot::plot_scene3d_render_item_source(*detached).publish(detached_snapshot,
                                                                           {.debug_name = "DetachedPlotScene3D"}),
                "detached chart snapshot publication failed");
        const tc_render_item* line_render_item = find_render_item(detached_snapshot, line);
        require(detached_snapshot.item_count() == 2 && line_render_item &&
                    line_render_item->kind == tcplot::PLOT_RENDER_ITEM_KIND_LINE,
                "detached chart did not publish its retained line item");
        const auto* line_payload = tcplot::plot_scene3d_render_item_payload(*line_render_item);
        require(line_payload && line_payload->item && line_payload->item->draw_vertex_count == 4,
                "retained line draw stream must contain two line segments");
        require(tc_retained_chart3d_attach_gpu_host(detached, &host) != 0,
                "failed to attach detached chart to its GPU host");
        tc_retained_chart3d_destroy(detached);

        tc_retained_chart3d* chart = tc_retained_chart3d_create(&host);
        tc_retained_chart3d* other = tc_retained_chart3d_create(&host);
        require(chart != nullptr && other != nullptr, "failed to create charts");
        require(tc_retained_chart3d_scene_id(chart) != tc_retained_chart3d_scene_id(other),
                "chart scene ids must be unique");
        require(tc_retained_chart3d_item_count(chart) == 1, "chart must create one default grid part");

        termin::RenderItemSnapshot empty_snapshot;
        const tc_plot_item3d_handle other_grid = tc_retained_chart3d_grid_part(other);
        require(tc_retained_chart3d_destroy_item(other, other_grid) != 0,
                "failed to remove the other chart default grid");
        termin::RenderItemSource& empty_source = tcplot::plot_scene3d_render_item_source(*other);
        require(empty_source.publish(empty_snapshot, {.debug_name = "EmptyPlotScene3D"}),
                "empty PlotScene3D source publication failed");
        require(empty_snapshot.valid() && empty_snapshot.item_count() == 0 &&
                    empty_snapshot.counters().source_traversals == 1 && empty_snapshot.counters().producers == 0,
                "empty PlotScene3D snapshot counters are invalid");

        const double x[] = {-1.0, 1.0, -1.0, 1.0};
        const double y[] = {-1.0, -1.0, 1.0, 1.0};
        const double z[] = {0.0, 0.5, 1.0, 0.25};
        tc_surface_item3d_style surface_style{
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            TC_PLOT_COLORMAP3D_VIRIDIS,
            0,
            0,
            1,
            1,
            1,
            1.0f,
        };
        const tc_plot_item3d_handle surface = tc_retained_chart3d_add_surface(chart, x, y, z, 2, 2, &surface_style);
        require(tc_retained_chart3d_item_is_valid(chart, surface) != 0, "surface handle must be valid in its scene");
        require(tc_retained_chart3d_item_is_valid(other, surface) == 0, "cross-scene surface handle must be rejected");
        require(tc_retained_chart3d_surface_set_style(other, surface, &surface_style) == 0,
                "cross-scene surface mutation must be rejected");
        tc_colorbar3d_style colorbar_style{
            5,
            18.0f,
            0.62f,
            18.0f,
            8.0f,
            13.0f,
            0.8f,
            0.8f,
            0.8f,
            1.0f,
            0.42f,
            0.45f,
            0.52f,
            1.0f,
        };
        require(tc_retained_chart3d_set_colorbar(chart, surface, "height", &colorbar_style) != 0,
                "failed to enable the retained colorbar");
        require(tc_retained_chart3d_set_colorbar(other, surface, "height", &colorbar_style) == 0,
                "cross-scene colorbar source must be rejected");
        tc_colorbar3d_style invalid_colorbar_style = colorbar_style;
        invalid_colorbar_style.tick_count = 1;
        require(tc_retained_chart3d_set_colorbar(chart, surface, "height", &invalid_colorbar_style) == 0,
                "invalid colorbar style must be rejected");

        const double scatter_x[] = {-0.5, 0.0, 0.75};
        const double scatter_y[] = {0.5, -0.25, 0.25};
        const double scatter_z[] = {0.25, 0.75, 0.5};
        tc_scatter_item3d_style scatter_style{
            1.0f,
            0.25f,
            0.1f,
            1.0f,
            5.0f,
        };
        const tc_plot_item3d_handle scatter =
            tc_retained_chart3d_add_scatter(chart, scatter_x, scatter_y, scatter_z, 3, &scatter_style);
        require(tc_retained_chart3d_set_colorbar(chart, scatter, "invalid", &colorbar_style) == 0,
                "a colorbar must reject a non-surface source");

        termin::RenderItemSource& render_item_source = tcplot::plot_scene3d_render_item_source(*chart);
        termin::RenderViewState first_view;
        termin::RenderViewState second_view;
        termin::RenderItemSnapshot first_render_snapshot;
        termin::RenderItemSnapshot second_render_snapshot;
        require(render_item_source.publish(first_render_snapshot,
                                           {.view = &first_view, .debug_name = "PlotScene3D first view"}),
                "first PlotScene3D source publication failed");
        require(render_item_source.publish(second_render_snapshot,
                                           {.view = &second_view, .debug_name = "PlotScene3D second view"}),
                "second PlotScene3D source publication failed");
        require(first_render_snapshot.valid() && second_render_snapshot.valid() &&
                    first_render_snapshot.item_count() == 3 && second_render_snapshot.item_count() == 3,
                "multi-view PlotScene3D snapshots must remain independently valid");
        require(first_render_snapshot.counters().source_traversals == 1 &&
                    first_render_snapshot.counters().producers == 3,
                "PlotScene3D snapshot counters are invalid");

        const tc_render_item* surface_render_item = find_render_item(first_render_snapshot, surface);
        const tc_render_item* scatter_render_item = find_render_item(first_render_snapshot, scatter);
        const tc_render_item* grid_render_item =
            find_render_item(first_render_snapshot, tc_retained_chart3d_grid_part(chart));
        require(surface_render_item && scatter_render_item && grid_render_item,
                "PlotScene3D snapshot lost retained item identity");
        require(surface_render_item->kind == tcplot::PLOT_RENDER_ITEM_KIND_SURFACE &&
                    scatter_render_item->kind == tcplot::PLOT_RENDER_ITEM_KIND_SCATTER &&
                    grid_render_item->kind == tcplot::PLOT_RENDER_ITEM_KIND_GRID,
                "PlotScene3D snapshot published incorrect item kinds");
        for (const tc_render_item& item : first_render_snapshot.items()) {
            require(item.source.domain_id == tcplot::PLOT_RENDER_ITEM_SOURCE_DOMAIN &&
                        item.source.namespace_id == tc_retained_chart3d_scene_id(chart) &&
                        item.source.adapter_data != 0 && tcplot::plot_scene3d_render_item_payload(item) != nullptr,
                    "PlotScene3D item must retain an immutable adapter payload");
        }
        const tcplot::PlotScene3DRenderItemPayload* first_surface_payload =
            find_plot_payload(first_render_snapshot, surface);
        const tcplot::PlotScene3DRenderItemPayload* first_scatter_payload =
            find_plot_payload(first_render_snapshot, scatter);
        const tcplot::PlotScene3DRenderItemPayload* first_grid_payload =
            find_plot_payload(first_render_snapshot, tc_retained_chart3d_grid_part(chart));
        require(first_surface_payload && first_surface_payload->item && first_scatter_payload &&
                    first_scatter_payload->item && first_grid_payload && first_grid_payload->item,
                "PlotScene3D payload lookup failed");
        require(first_surface_payload->item->z == std::vector<double>(std::begin(z), std::end(z)) &&
                    first_surface_payload->item->surface_style.wireframe == 0 &&
                    first_surface_payload->item->draw_vertex_count == 6 &&
                    first_surface_payload->item->draw_vertices.size() == 6u * 19u &&
                    first_scatter_payload->item->x == std::vector<double>(std::begin(scatter_x), std::end(scatter_x)) &&
                    first_scatter_payload->item->draw_vertex_count == 18 &&
                    first_scatter_payload->item->draw_vertices.size() == 18u * 19u &&
                    first_grid_payload->item->draw_vertex_count >= 6 &&
                    first_grid_payload->item->draw_vertex_count % 2 == 0 &&
                    first_grid_payload->item->draw_vertices.size() ==
                        first_grid_payload->item->draw_vertex_count * 19u &&
                    first_surface_payload->frame.x_label == "x",
                "PlotScene3D payload lost item or chart values");
        const float expected_scatter_cross_size =
            static_cast<float>(std::sqrt(1.25 * 1.25 + 0.75 * 0.75 + 0.5 * 0.5) * 0.008 * (scatter_style.size / 4.0));
        const termin::LinearColor expected_scatter_color = termin::srgb_to_linear(
            termin::SrgbColor{scatter_style.color_r, scatter_style.color_g, scatter_style.color_b, scatter_style.color_a});
        require(std::abs(first_scatter_payload->item->draw_vertices[0] - (-0.5f - expected_scatter_cross_size)) <
                        1e-6f &&
                    first_scatter_payload->item->draw_vertices[1] == 0.5f &&
                    first_scatter_payload->item->draw_vertices[2] == 0.25f &&
                    std::abs(first_scatter_payload->item->draw_vertices[3] - expected_scatter_color.r) < 1e-6f &&
                    std::abs(first_scatter_payload->item->draw_vertices[4] - expected_scatter_color.g) < 1e-6f &&
                    std::abs(first_scatter_payload->item->draw_vertices[5] - expected_scatter_color.b) < 1e-6f,
                "PlotScene3D scatter stream lost geometry or linear color semantics");

        termin::RenderItemEncoderCapabilities surface_capabilities{};
        require(
            termin::get_render_item_encoder_capabilities(tcplot::PLOT_RENDER_ITEM_KIND_SURFACE, surface_capabilities) &&
                surface_capabilities.phase_mask == TC_PHASE_OPAQUE && surface_capabilities.requires_draw_context &&
                !surface_capabilities.consumes_common_resources,
            "PlotScene3D surface encoder capabilities are invalid");
        termin::RenderItemTaskPlanningContract surface_contract{};
        surface_contract.phase = TC_PHASE_OPAQUE;
        surface_contract.material_phase_policy = termin::RenderItemMaterialPhasePolicy::Forbidden;
        surface_contract.provided_input_mask =
            termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
        surface_contract.required_input_mask = surface_contract.provided_input_mask;
        surface_contract.debug_pass_name = "PlotScene3D surface planning test";
        termin::RenderItemTaskPlanningRequest surface_planning{};
        surface_planning.item = surface_render_item;
        surface_planning.item_index = 0;
        surface_planning.source_draw_index = 0;
        surface_planning.contract = &surface_contract;
        termin::RenderTaskList surface_tasks;
        const termin::RenderItemTaskPlanningResult surface_plan =
            termin::plan_render_item_task(surface_planning, surface_tasks);
        require(surface_plan.accepted() && surface_tasks.size() == 1 &&
                    !tc_shader_handle_is_invalid(surface_tasks.at(surface_plan.task_index).final_shader),
                "PlotScene3D surface task planning failed");

        termin::RenderItemEncoderCapabilities scatter_capabilities{};
        require(
            termin::get_render_item_encoder_capabilities(tcplot::PLOT_RENDER_ITEM_KIND_SCATTER, scatter_capabilities) &&
                scatter_capabilities.phase_mask == TC_PHASE_OPAQUE && scatter_capabilities.requires_draw_context &&
                !scatter_capabilities.consumes_common_resources,
            "PlotScene3D scatter encoder capabilities are invalid");
        termin::RenderItemTaskPlanningRequest scatter_planning = surface_planning;
        scatter_planning.item = scatter_render_item;
        scatter_planning.item_index = 1;
        scatter_planning.source_draw_index = 1;
        termin::RenderTaskList scatter_tasks;
        const termin::RenderItemTaskPlanningResult scatter_plan =
            termin::plan_render_item_task(scatter_planning, scatter_tasks);
        require(scatter_plan.accepted() && scatter_tasks.size() == 1 &&
                    !tc_shader_handle_is_invalid(scatter_tasks.at(scatter_plan.task_index).final_shader),
                "PlotScene3D scatter task planning failed");

        termin::RenderItemEncoderCapabilities grid_capabilities{};
        require(termin::get_render_item_encoder_capabilities(tcplot::PLOT_RENDER_ITEM_KIND_GRID, grid_capabilities) &&
                    grid_capabilities.phase_mask == TC_PHASE_OPAQUE && grid_capabilities.requires_draw_context &&
                    !grid_capabilities.consumes_common_resources,
                "PlotScene3D grid encoder capabilities are invalid");
        termin::RenderItemTaskPlanningRequest grid_planning = surface_planning;
        grid_planning.item = grid_render_item;
        grid_planning.item_index = 2;
        grid_planning.source_draw_index = 2;
        termin::RenderTaskList grid_tasks;
        const termin::RenderItemTaskPlanningResult grid_plan = termin::plan_render_item_task(grid_planning, grid_tasks);
        require(grid_plan.accepted() && grid_tasks.size() == 1 &&
                    !tc_shader_handle_is_invalid(grid_tasks.at(grid_plan.task_index).final_shader),
                "PlotScene3D grid task planning failed");

        auto malformed_surface_data = std::make_shared<tcplot::PlotScene3DItemRenderData>(*first_surface_payload->item);
        malformed_surface_data->draw_vertices.clear();
        malformed_surface_data->draw_vertex_count = 0;
        tcplot::PlotScene3DRenderItemPayload malformed_surface_payload = *first_surface_payload;
        malformed_surface_payload.item = std::move(malformed_surface_data);
        tc_render_item malformed_surface_item = *surface_render_item;
        malformed_surface_item.source.adapter_data = reinterpret_cast<uintptr_t>(&malformed_surface_payload);
        termin::RenderContext malformed_draw_context;
        malformed_draw_context.phase = TC_PHASE_OPAQUE;
        malformed_draw_context.viewport_width = 320;
        malformed_draw_context.viewport_height = 240;
        termin::RenderItemDrawSubmitRequest malformed_submission{};
        malformed_submission.shader_handle = surface_tasks.at(surface_plan.task_index).final_shader;
        malformed_submission.device = &host.device();
        malformed_submission.draw_context = &malformed_draw_context;
        malformed_submission.phase = TC_PHASE_OPAQUE;
        malformed_submission.debug_pass_name = "PlotScene3D malformed surface test";
        require(!termin::submit_render_item_draw(host.ctx(), malformed_surface_item, malformed_submission),
                "PlotScene3D surface encoder accepted a malformed draw stream");

        auto malformed_scatter_data = std::make_shared<tcplot::PlotScene3DItemRenderData>(*first_scatter_payload->item);
        malformed_scatter_data->draw_vertices.clear();
        malformed_scatter_data->draw_vertex_count = 0;
        tcplot::PlotScene3DRenderItemPayload malformed_scatter_payload = *first_scatter_payload;
        malformed_scatter_payload.item = std::move(malformed_scatter_data);
        tc_render_item malformed_scatter_item = *scatter_render_item;
        malformed_scatter_item.source.adapter_data = reinterpret_cast<uintptr_t>(&malformed_scatter_payload);
        malformed_submission.shader_handle = scatter_tasks.at(scatter_plan.task_index).final_shader;
        malformed_submission.debug_pass_name = "PlotScene3D malformed scatter test";
        require(!termin::submit_render_item_draw(host.ctx(), malformed_scatter_item, malformed_submission),
                "PlotScene3D scatter encoder accepted a malformed draw stream");

        auto malformed_grid_data = std::make_shared<tcplot::PlotScene3DItemRenderData>(*first_grid_payload->item);
        malformed_grid_data->draw_vertices.clear();
        malformed_grid_data->draw_vertex_count = 0;
        tcplot::PlotScene3DRenderItemPayload malformed_grid_payload = *first_grid_payload;
        malformed_grid_payload.item = std::move(malformed_grid_data);
        tc_render_item malformed_grid_item = *grid_render_item;
        malformed_grid_item.source.adapter_data = reinterpret_cast<uintptr_t>(&malformed_grid_payload);
        malformed_submission.shader_handle = grid_tasks.at(grid_plan.task_index).final_shader;
        malformed_submission.debug_pass_name = "PlotScene3D malformed grid test";
        require(!termin::submit_render_item_draw(host.ctx(), malformed_grid_item, malformed_submission),
                "PlotScene3D grid encoder accepted a malformed draw stream");

        termin::RenderItemTaskPlanningContract unsupported_surface_contract = surface_contract;
        unsupported_surface_contract.phase = TC_PHASE_TRANSPARENT;
        surface_planning.contract = &unsupported_surface_contract;
        termin::RenderTaskList unsupported_surface_tasks;
        require(termin::plan_render_item_task(surface_planning, unsupported_surface_tasks).rejection ==
                        termin::RenderItemTaskRejection::PassOutputUnsupported &&
                    unsupported_surface_tasks.empty(),
                "PlotScene3D surface planner accepted an unsupported output");

        termin::RenderItemTaskPlanningContract missing_input_contract = surface_contract;
        missing_input_contract.provided_input_mask = 0;
        surface_planning.contract = &missing_input_contract;
        termin::RenderTaskList missing_input_tasks;
        require(termin::plan_render_item_task(surface_planning, missing_input_tasks).rejection ==
                        termin::RenderItemTaskRejection::RequiredInputMissing &&
                    missing_input_tasks.empty(),
                "PlotScene3D surface planner accepted missing draw context input");
        surface_planning.contract = &surface_contract;
        surface_planning.material_phase = reinterpret_cast<tc_material_phase*>(uintptr_t{1});
        termin::RenderTaskList material_surface_tasks;
        require(termin::plan_render_item_task(surface_planning, material_surface_tasks).rejection ==
                        termin::RenderItemTaskRejection::MaterialPhaseForbidden &&
                    material_surface_tasks.empty(),
                "PlotScene3D surface planner accepted a material phase");
        surface_planning.material_phase = nullptr;

        scatter_planning.contract = &unsupported_surface_contract;
        termin::RenderTaskList unsupported_scatter_tasks;
        require(termin::plan_render_item_task(scatter_planning, unsupported_scatter_tasks).rejection ==
                        termin::RenderItemTaskRejection::PassOutputUnsupported &&
                    unsupported_scatter_tasks.empty(),
                "PlotScene3D scatter planner accepted an unsupported output");
        scatter_planning.contract = &missing_input_contract;
        termin::RenderTaskList missing_scatter_input_tasks;
        require(termin::plan_render_item_task(scatter_planning, missing_scatter_input_tasks).rejection ==
                        termin::RenderItemTaskRejection::RequiredInputMissing &&
                    missing_scatter_input_tasks.empty(),
                "PlotScene3D scatter planner accepted missing draw context input");
        scatter_planning.contract = &surface_contract;
        scatter_planning.material_phase = reinterpret_cast<tc_material_phase*>(uintptr_t{1});
        termin::RenderTaskList material_scatter_tasks;
        require(termin::plan_render_item_task(scatter_planning, material_scatter_tasks).rejection ==
                        termin::RenderItemTaskRejection::MaterialPhaseForbidden &&
                    material_scatter_tasks.empty(),
                "PlotScene3D scatter planner accepted a material phase");
        scatter_planning.material_phase = nullptr;

        const tc_plot_item3d_handle isolated_scatter =
            tc_retained_chart3d_add_scatter(other, scatter_x, scatter_y, scatter_z, 3, &scatter_style);
        const uint32_t isolated_scatter_texture = tc_retained_chart3d_render(other, 320, 240);
        require(isolated_scatter_texture != 0 && snapshot(other, isolated_scatter).gpu_revision != 0 &&
                    count_non_clear_pixels(host, isolated_scatter_texture, 320, 240) > 5,
                "PlotScene3D scatter encoder produced no visible output");
        const tcplot::PlotScene3DRenderItemPayload* second_surface_payload =
            find_plot_payload(second_render_snapshot, surface);
        require(second_surface_payload && second_surface_payload->item == first_surface_payload->item,
                "unchanged item data must be shared between snapshots");
        execute_snapshot_probe(first_render_snapshot, host);

        const double changed_z[] = {0.25, 0.75, 1.25, 0.5};
        require(tc_retained_chart3d_surface_set_data(chart, surface, x, y, changed_z, 2, 2) != 0,
                "failed to update surface data for snapshot isolation");
        termin::RenderItemSnapshot geometry_changed_snapshot;
        require(render_item_source.publish(geometry_changed_snapshot, {}),
                "PlotScene3D publication after geometry mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* changed_surface_payload =
            find_plot_payload(geometry_changed_snapshot, surface);
        const tcplot::PlotScene3DRenderItemPayload* changed_grid_payload =
            find_plot_payload(geometry_changed_snapshot, tc_retained_chart3d_grid_part(chart));
        require(changed_surface_payload && changed_surface_payload->item &&
                    changed_surface_payload->item->z ==
                        std::vector<double>(std::begin(changed_z), std::end(changed_z)) &&
                    changed_surface_payload->geometry_revision == first_surface_payload->geometry_revision + 1 &&
                    changed_surface_payload->item != first_surface_payload->item,
                "geometry mutation must publish a new immutable item payload");
        require(changed_grid_payload && changed_grid_payload->item &&
                    changed_grid_payload->item != first_grid_payload->item &&
                    changed_grid_payload->geometry_revision == first_grid_payload->geometry_revision + 1 &&
                    changed_grid_payload->frame.bounds_max[2] == 1.25 && first_grid_payload->frame.bounds_max[2] == 1.0,
                "chart bounds mutation must replace the immutable grid stream");
        require(first_surface_payload->item->z == std::vector<double>(std::begin(z), std::end(z)) &&
                    find_plot_payload(geometry_changed_snapshot, scatter)->item == first_scatter_payload->item,
                "geometry mutation changed an older or unrelated snapshot payload");

        const double changed_scatter_x[] = {-1.0, 0.0, 1.0};
        const double changed_scatter_y[] = {0.0, 0.75, -0.5};
        const double changed_scatter_z[] = {0.25, 1.0, 0.5};
        require(tc_retained_chart3d_scatter_set_data(
                    chart, scatter, changed_scatter_x, changed_scatter_y, changed_scatter_z, 3) != 0,
                "failed to update scatter data for snapshot isolation");
        termin::RenderItemSnapshot scatter_geometry_snapshot;
        require(render_item_source.publish(scatter_geometry_snapshot, {}),
                "PlotScene3D publication after scatter mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* changed_scatter_payload =
            find_plot_payload(scatter_geometry_snapshot, scatter);
        require(changed_scatter_payload && changed_scatter_payload->item &&
                    changed_scatter_payload->item->x ==
                        std::vector<double>(std::begin(changed_scatter_x), std::end(changed_scatter_x)) &&
                    changed_scatter_payload->item->draw_vertex_count == 18 &&
                    changed_scatter_payload->geometry_revision == first_scatter_payload->geometry_revision + 1 &&
                    changed_scatter_payload->item != first_scatter_payload->item &&
                    find_plot_payload(scatter_geometry_snapshot, surface)->item == changed_surface_payload->item,
                "scatter mutation must replace only its immutable item payload");
        require(first_scatter_payload->item->x == std::vector<double>(std::begin(scatter_x), std::end(scatter_x)),
                "scatter mutation altered an older snapshot payload");

        const auto surface_initial = snapshot(chart, surface);
        const auto scatter_initial = snapshot(chart, scatter);
        require(tc_retained_chart3d_surface_set_style(chart, surface, &surface_style) != 0,
                "identical surface style must be accepted");
        require(same_snapshot(surface_initial, snapshot(chart, surface)), "identical style must be a no-op");

        tc_orbit_camera3d_state camera{};
        require(tc_retained_chart3d_get_camera(chart, &camera) != 0, "failed to read camera");
        camera.azimuth += 0.25f;
        require(tc_retained_chart3d_set_camera(chart, &camera) != 0, "failed to update camera");
        require(tc_retained_chart3d_set_surface_shading(chart, 1, 0.4f) != 0 &&
                    tc_retained_chart3d_set_surface_shading(chart, 1, std::numeric_limits<float>::quiet_NaN()) == 0,
                "shading validation must be observable by callers");
        require(tc_retained_chart3d_set_light_direction(chart, 0, 0, 0) == 0 &&
                    tc_retained_chart3d_set_axis_scale(chart, 1, -1, 1) == 0,
                "invalid chart policy values must be rejected");
        require(same_snapshot(surface_initial, snapshot(chart, surface)) &&
                    same_snapshot(scatter_initial, snapshot(chart, scatter)),
                "camera changes must not invalidate item state");
        termin::RenderItemSnapshot chart_state_snapshot;
        require(render_item_source.publish(chart_state_snapshot, {}),
                "PlotScene3D publication after chart-state mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* chart_state_payload =
            find_plot_payload(chart_state_snapshot, surface);
        require(chart_state_payload && chart_state_payload->frame.camera.azimuth == camera.azimuth &&
                    chart_state_payload->frame.surface_shading_strength == 0.4f &&
                    first_surface_payload->frame.camera.azimuth != camera.azimuth,
                "chart-state mutation must publish values without altering older snapshots");

        tgfx::set_builtin_shader_read_callback([](std::string_view path, std::string& contents) {
            if (!path.ends_with("termin-engine-tcplot-3d.slang")) {
                return false;
            }
            contents = "invalid PlotScene3D shader source";
            return true;
        });
        const tc_shader_handle plot_shader = tc_shader_find("termin-engine-tcplot-3d");
        if (tc_shader_is_valid(plot_shader)) {
            require(tc_shader_destroy(plot_shader), "failed to invalidate the plot shader for failure testing");
        }
        require(tc_retained_chart3d_render(chart, 320, 240) == 0,
                "shader preparation failure must be visible through the public render result");
        require(snapshot(chart, surface).gpu_revision == 0 && snapshot(chart, scatter).gpu_revision == 0 &&
                    snapshot(chart, tc_retained_chart3d_grid_part(chart)).gpu_revision == 0,
                "failed retained render must leave every item dirty");

        tgfx::set_builtin_shader_read_callback({});
        const tc_shader_handle failed_plot_shader = tc_shader_find("termin-engine-tcplot-3d");
        if (tc_shader_is_valid(failed_plot_shader)) {
            require(tc_shader_destroy(failed_plot_shader), "failed to remove the rejected plot shader");
        }
        const uint32_t first_render_texture = tc_retained_chart3d_render(chart, 320, 240);
        require(first_render_texture != 0, "initial retained render failed");
        tgfx::TextureHandle first_render_handle{};
        first_render_handle.id = first_render_texture;
        require(host.device().texture_desc(first_render_handle).sample_count == 1,
                "RetainedChart3D must publish a resolved single-sample texture");
        const std::size_t labeled_pixel_count = count_non_clear_pixels(host, first_render_texture, 320, 240);
        require(labeled_pixel_count > 100, "PlotScene3D retained encoders produced no visible output");
        const auto surface_rendered = snapshot(chart, surface);
        const auto scatter_rendered = snapshot(chart, scatter);
        const auto grid_rendered = snapshot(chart, tc_retained_chart3d_grid_part(chart));
        require(surface_rendered.gpu_revision != 0 && scatter_rendered.gpu_revision != 0 &&
                    grid_rendered.gpu_revision != 0,
                "render must synchronize item GPU revisions");

        require(tc_retained_chart3d_set_msaa_samples(chart, 2) != 0 &&
                    tc_retained_chart3d_set_msaa_samples(chart, 3) == 0,
                "retained Chart3D MSAA validation failed");
        const uint32_t two_sample_render_texture = tc_retained_chart3d_render(chart, 320, 240);
        tgfx::TextureHandle two_sample_render_handle{};
        two_sample_render_handle.id = two_sample_render_texture;
        const std::size_t two_sample_labeled_pixel_count =
            count_non_clear_pixels(host, two_sample_render_texture, 320, 240);
        const std::vector<float> visible_labels_pixels =
            read_pixels(host, two_sample_render_texture, 320, 240);
        require(two_sample_render_texture != 0 &&
                    host.device().texture_desc(two_sample_render_handle).sample_count == 1 &&
                    two_sample_labeled_pixel_count > 100,
                "MSAA Chart3D render was not resolved to a visible single-sample output");

        tc_grid_item3d_style grid_style{};
        const tc_plot_item3d_handle grid = tc_retained_chart3d_grid_part(chart);
        require(tc_retained_chart3d_grid_get_style(chart, grid, &grid_style) != 0 && grid_style.labels_visible != 0,
                "failed to read the retained grid style");
        grid_style.labels_visible = 0;
        require(tc_retained_chart3d_grid_set_style(chart, grid, &grid_style) != 0,
                "failed to disable retained grid labels");
        termin::RenderItemSnapshot hidden_grid_snapshot;
        require(render_item_source.publish(hidden_grid_snapshot, {}),
                "PlotScene3D publication after grid style mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* hidden_grid_payload = find_plot_payload(hidden_grid_snapshot, grid);
        require(hidden_grid_payload && hidden_grid_payload->item &&
                    hidden_grid_payload->item->grid_style.labels_visible == 0 &&
                    hidden_grid_payload->style_revision == first_grid_payload->style_revision + 1 &&
                    first_grid_payload->item->grid_style.labels_visible != 0,
                "grid style mutation altered an older snapshot payload");
        const uint32_t hidden_labels_texture = tc_retained_chart3d_render(chart, 320, 240);
        const std::vector<float> hidden_labels_pixels = read_pixels(host, hidden_labels_texture, 320, 240);
        require(hidden_labels_texture != 0 &&
                    count_changed_pixels(visible_labels_pixels, hidden_labels_pixels) > 10,
                "chart-owned grid labels produced no visible annotation pixels");
        grid_style.labels_visible = 1;
        require(tc_retained_chart3d_grid_set_style(chart, grid, &grid_style) != 0 &&
                    tc_retained_chart3d_render(chart, 320, 240) != 0,
                "failed to restore retained grid labels");

        surface_style.wireframe = 1;
        require(tc_retained_chart3d_surface_set_style(chart, surface, &surface_style) != 0,
                "failed to update surface style");
        const auto surface_invalidated = snapshot(chart, surface);
        require(surface_invalidated.geometry_revision == surface_rendered.geometry_revision &&
                    surface_invalidated.style_revision == surface_rendered.style_revision + 1 &&
                    surface_invalidated.gpu_revision == 0,
                "style change must preserve semantic geometry and invalidate GPU state");
        termin::RenderItemSnapshot style_changed_snapshot;
        require(render_item_source.publish(style_changed_snapshot, {}),
                "PlotScene3D publication after style mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* styled_surface_payload =
            find_plot_payload(style_changed_snapshot, surface);
        require(styled_surface_payload && styled_surface_payload->item &&
                    styled_surface_payload->item->surface_style.wireframe == 1 &&
                    styled_surface_payload->item->draw_vertex_count == 12 &&
                    first_surface_payload->item->draw_vertex_count == 6 &&
                    first_surface_payload->item->surface_style.wireframe == 0 &&
                    styled_surface_payload->style_revision == first_surface_payload->style_revision + 1,
                "style mutation must not alter an older snapshot payload");
        require(same_snapshot(scatter_rendered, snapshot(chart, scatter)),
                "surface style must not invalidate unrelated scatter");

        scatter_style.size = 8.0f;
        scatter_style.color_g = 0.75f;
        require(tc_retained_chart3d_scatter_set_style(chart, scatter, &scatter_style) != 0,
                "failed to update scatter style");
        const auto scatter_invalidated = snapshot(chart, scatter);
        require(scatter_invalidated.geometry_revision == scatter_rendered.geometry_revision &&
                    scatter_invalidated.style_revision == scatter_rendered.style_revision + 1 &&
                    scatter_invalidated.gpu_revision == 0 &&
                    same_snapshot(surface_invalidated, snapshot(chart, surface)),
                "scatter style change must invalidate only scatter GPU state");
        termin::RenderItemSnapshot scatter_style_snapshot;
        require(render_item_source.publish(scatter_style_snapshot, {}),
                "PlotScene3D publication after scatter style mutation failed");
        const tcplot::PlotScene3DRenderItemPayload* styled_scatter_payload =
            find_plot_payload(scatter_style_snapshot, scatter);
        require(styled_scatter_payload && styled_scatter_payload->item &&
                    styled_scatter_payload->item->scatter_style.size == 8.0f &&
                    styled_scatter_payload->item->draw_vertex_count == 18 &&
                    styled_scatter_payload->item != changed_scatter_payload->item &&
                    changed_scatter_payload->item->scatter_style.size == 5.0f &&
                    styled_scatter_payload->style_revision == changed_scatter_payload->style_revision + 1,
                "scatter style mutation must replace its immutable draw stream");

        tc_scatter_item3d_style invalid_scatter_style = scatter_style;
        invalid_scatter_style.size = -1.0f;
        require(tc_retained_chart3d_scatter_set_style(chart, scatter, &invalid_scatter_style) == 0 &&
                    same_snapshot(scatter_invalidated, snapshot(chart, scatter)),
                "rejected scatter style must not mutate item revisions");

        tc_surface_item3d_style invalid_style = surface_style;
        invalid_style.surface_grid_width_px = -1.0f;
        require(tc_retained_chart3d_surface_set_style(chart, surface, &invalid_style) == 0,
                "invalid surface style must be rejected");
        require(same_snapshot(surface_invalidated, snapshot(chart, surface)),
                "rejected style must not mutate item revisions");

        require(tc_retained_chart3d_render(chart, 512, 256) != 0, "resized retained render failed");
        require(snapshot(chart, surface).gpu_revision != 0, "style update must be synchronized by render");
        const auto scatter_after_style_render = snapshot(chart, scatter);
        require(scatter_after_style_render.geometry_revision == scatter_invalidated.geometry_revision &&
                    scatter_after_style_render.style_revision == scatter_invalidated.style_revision &&
                    scatter_after_style_render.gpu_revision != 0,
                "resized render must submit scatter without rebuilding its stream");

        tc_retained_chart3d_release_gpu(chart);
        require(snapshot(chart, surface).gpu_revision == 0 && snapshot(chart, scatter).gpu_revision == 0 &&
                    snapshot(chart, tc_retained_chart3d_grid_part(chart)).gpu_revision == 0,
                "GPU release must invalidate item GPU revisions");
        require(tc_retained_chart3d_render(chart, 512, 256) != 0, "render after GPU release failed");
        require(snapshot(chart, surface).gpu_revision != 0 && snapshot(chart, scatter).gpu_revision != 0 &&
                    snapshot(chart, tc_retained_chart3d_grid_part(chart)).gpu_revision != 0,
                "render after GPU release must rebuild item resources");

        require(tc_retained_chart3d_destroy_item(chart, scatter) != 0, "failed to destroy scatter");
        require(tc_retained_chart3d_item_is_valid(chart, scatter) == 0, "destroyed handle must be stale");
        termin::RenderItemSnapshot after_destroy_snapshot;
        require(render_item_source.publish(after_destroy_snapshot, {}), "PlotScene3D publication after destroy failed");
        require(find_render_item(after_destroy_snapshot, scatter) == nullptr,
                "destroyed item must disappear from PlotScene3D snapshots");
        const tc_plot_item3d_handle replacement =
            tc_retained_chart3d_add_scatter(chart, scatter_x, scatter_y, scatter_z, 3, &scatter_style);
        require(replacement.index == scatter.index && replacement.generation != scatter.generation,
                "reused slot must advance its generation");
        termin::RenderItemSnapshot replacement_snapshot;
        require(render_item_source.publish(replacement_snapshot, {}),
                "PlotScene3D publication after slot reuse failed");
        require(find_render_item(replacement_snapshot, scatter) == nullptr &&
                    find_render_item(replacement_snapshot, replacement) != nullptr,
                "PlotScene3D snapshot must publish only the live slot generation");
        const tcplot::PlotScene3DRenderItemPayload* replacement_payload =
            find_plot_payload(replacement_snapshot, replacement);
        require(replacement_payload && replacement_payload->item &&
                    replacement_payload->item != first_scatter_payload->item,
                "reused slot must not recycle the previous generation's payload");
        require(tc_retained_chart3d_destroy_item(chart, scatter) == 0,
                "stale handle must not destroy replacement item");

        const tc_plot_item3d_handle retained_grid = tc_retained_chart3d_grid_part(chart);
        tc_retained_chart3d_clear_data(chart);
        require(tc_retained_chart3d_item_count(chart) == 1 &&
                    tc_retained_chart3d_item_is_valid(chart, retained_grid) != 0 &&
                    tc_retained_chart3d_item_is_valid(chart, surface) == 0 &&
                    tc_retained_chart3d_item_is_valid(chart, replacement) == 0,
                "clear_data must preserve only the configured grid");

        const tc_plot_item3d_handle reattached_surface =
            tc_retained_chart3d_add_surface(chart, x, y, z, 2, 2, &surface_style);

        tc_retained_chart3d_detach_gpu_host(chart);
        require(snapshot(chart, reattached_surface).gpu_revision == 0,
                "GPU-host detach must preserve CPU items and invalidate their GPU revisions");
        require(tc_retained_chart3d_attach_gpu_host(chart, &host) != 0 &&
                    tc_retained_chart3d_render(chart, 320, 240) != 0,
                "detached retained chart did not reattach and render");

        tc_retained_chart3d_destroy(other);
        tc_retained_chart3d_destroy(chart);
        require(first_surface_payload->item->z == std::vector<double>(std::begin(z), std::end(z)) &&
                    first_surface_payload->frame.x_label == "x" &&
                    first_surface_payload->item->surface_style.wireframe == 0,
                "snapshot-owned PlotScene3D payload did not survive chart destruction");
        std::printf("retained Chart3D lifecycle and invalidation test passed\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "retained Chart3D test failed: %s\n", error.what());
        return 1;
    }
}
