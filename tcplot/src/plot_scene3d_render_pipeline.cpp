#include "plot_scene3d_render_pipeline.hpp"

#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/render/render_execution_capabilities.hpp>
#include <termin/render/render_item_snapshot.hpp>
#include <termin/render/render_item_submission.hpp>
#include <termin/render/render_task.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/render_context.hpp>

#include "plot_scene3d_chart_chrome.hpp"
#include "tcplot/gpu_host.hpp"
#include "tcplot/plot_scene3d_render_item_source.hpp"

namespace tcplot {
    namespace {

        constexpr const char* kGeometryResource = "PlotScene3DGeometry";
        constexpr const char* kOutputResource = "OUTPUT";
        constexpr const char* kGeometryPassName = "RetainedChart3D/Geometry";
        constexpr const char* kChromePassName = "RetainedChart3D/Chrome";

        struct PlotScene3DExecutionReport {
            bool success = true;
            bool geometry_executed = false;
            bool chrome_executed = false;
            std::vector<PlotScene3DRenderedItem> rendered_items;
        };

        class PlotScene3DRenderServices final : public termin::RenderExecutionCapability {
        public:
            tgfx::FontAtlas* font = nullptr;
            std::uint64_t selected_grid_namespace = 0;
            std::uint64_t selected_grid_object = 0;
            std::uint32_t selected_grid_generation = 0;
            PlotScene3DExecutionReport* report = nullptr;
        };

        const PlotScene3DRenderServices* require_plot_services(termin::ExecuteContext& context, const char* pass_name) {
            const auto* services =
                context.capabilities ? context.capabilities->find<PlotScene3DRenderServices>() : nullptr;
            if (!services || !services->report) {
                tc::Log::error("[%s] PlotScene3DRenderServices capability is missing", pass_name);
                return nullptr;
            }
            return services;
        }

        bool is_plot_kind(std::uint32_t kind) {
            return kind == PLOT_RENDER_ITEM_KIND_SURFACE || kind == PLOT_RENDER_ITEM_KIND_GRID ||
                   kind == PLOT_RENDER_ITEM_KIND_SCATTER || kind == PLOT_RENDER_ITEM_KIND_LINE;
        }

        bool is_selected_grid(const tc_render_item& item, const PlotScene3DRenderServices& services) {
            return item.kind == PLOT_RENDER_ITEM_KIND_GRID && item.source.domain_id == PLOT_RENDER_ITEM_SOURCE_DOMAIN &&
                   item.source.namespace_id == services.selected_grid_namespace &&
                   item.source.object_id == services.selected_grid_object &&
                   item.source.generation == services.selected_grid_generation;
        }

        const char* plot_item_debug_name(std::uint32_t kind) {
            if (kind == PLOT_RENDER_ITEM_KIND_SURFACE) {
                return "PlotScene3D surface";
            }
            if (kind == PLOT_RENDER_ITEM_KIND_GRID) {
                return "PlotScene3D grid";
            }
            if (kind == PLOT_RENDER_ITEM_KIND_LINE) {
                return "PlotScene3D line";
            }
            return "PlotScene3D scatter";
        }

        class PlotScene3DGeometryPass final : public termin::CxxFramePass {
        public:
            PlotScene3DGeometryPass() {
                pass_name_set(kGeometryPassName);
            }

            std::set<const char*> compute_writes() const override {
                return {kGeometryResource};
            }

            std::vector<std::string> get_internal_symbols() const override {
                return internal_symbols_;
            }

            void execute(termin::ExecuteContext& context) override {
                const PlotScene3DRenderServices* services = require_plot_services(context, kGeometryPassName);
                if (!services) {
                    return;
                }
                PlotScene3DExecutionReport& report = *services->report;
                report.geometry_executed = true;

                if (!context.ctx2) {
                    tc::Log::error("[%s] render context is missing", kGeometryPassName);
                    report.success = false;
                    return;
                }
                if (!context.render_item_snapshot) {
                    tc::Log::error("[%s] render item snapshot is missing", kGeometryPassName);
                    report.success = false;
                    return;
                }

                const termin::FrameGraphColorAttachment color_attachment{
                    kGeometryResource,
                    tgfx::LoadOp::Load,
                    tgfx::StoreOp::Store,
                    {0.0f, 0.0f, 0.0f, 0.0f},
                };
                const termin::FrameGraphDepthAttachment depth_attachment{
                    kGeometryResource,
                    tgfx::LoadOp::Load,
                    tgfx::StoreOp::Store,
                    1.0f,
                    0,
                };
                tgfx::RenderPassDesc render_pass;
                if (!context.build_render_pass(std::span<const termin::FrameGraphColorAttachment>(&color_attachment, 1),
                                               &depth_attachment,
                                               render_pass) ||
                    !context.ctx2->begin_pass(render_pass)) {
                    tc::Log::error("[%s] failed to begin framegraph render pass", kGeometryPassName);
                    report.success = false;
                    return;
                }

                context.ctx2->set_viewport(0, 0, context.render_rect.width, context.render_rect.height);

                termin::RenderItemTaskPlanningContract contract{};
                contract.phase = TC_PHASE_OPAQUE;
                contract.material_phase_policy = termin::RenderItemMaterialPhasePolicy::Forbidden;
                contract.provided_input_mask =
                    termin::render_item_task_input_bit(termin::RenderItemTaskInput::DrawContext);
                contract.required_input_mask = contract.provided_input_mask;
                contract.debug_pass_name = kGeometryPassName;

                termin::RenderContext draw_context;
                draw_context.phase = TC_PHASE_OPAQUE;
                draw_context.viewport_width = context.render_rect.width;
                draw_context.viewport_height = context.render_rect.height;

                termin::RenderTaskList tasks;
                tasks.reserve(context.render_item_snapshot->item_count());
                internal_symbols_.clear();
                for (std::size_t item_index = 0; item_index < context.render_item_snapshot->item_count();
                     ++item_index) {
                    const tc_render_item* item = context.render_item_snapshot->item(item_index);
                    if (!item || !is_plot_kind(item->kind)) {
                        continue;
                    }
                    if (item->kind == PLOT_RENDER_ITEM_KIND_GRID && !is_selected_grid(*item, *services)) {
                        continue;
                    }

                    termin::RenderItemTaskPlanningRequest planning{};
                    planning.item = item;
                    planning.item_index = item_index;
                    planning.source_draw_index = item_index;
                    planning.contract = &contract;
                    const termin::RenderItemTaskPlanningResult result = termin::plan_render_item_task(planning, tasks);
                    if (!result.accepted()) {
                        tc::Log::error("[%s] item task planning failed for "
                                       "object %llu: %s",
                                       kGeometryPassName,
                                       static_cast<unsigned long long>(item->source.object_id),
                                       termin::render_item_task_rejection_name(result.rejection));
                        report.success = false;
                        continue;
                    }
                    termin::RenderTask& task = tasks.at(result.task_index);
                    task.draw_context = draw_context;
                    task.debug_name = plot_item_debug_name(item->kind);
                    internal_symbols_.push_back(task.debug_name);
                }

                constexpr std::array<std::uint32_t, 4> draw_order{
                    PLOT_RENDER_ITEM_KIND_SURFACE,
                    PLOT_RENDER_ITEM_KIND_GRID,
                    PLOT_RENDER_ITEM_KIND_LINE,
                    PLOT_RENDER_ITEM_KIND_SCATTER,
                };
                for (const std::uint32_t kind : draw_order) {
                    for (termin::RenderTask& task : tasks) {
                        if (!task.item || task.item->kind != kind) {
                            continue;
                        }
                        termin::RenderItemDrawSubmitRequest submission{};
                        submission.shader_handle = task.final_shader;
                        submission.device = &context.ctx2->device();
                        submission.draw_context = &task.draw_context;
                        submission.phase = task.phase;
                        submission.debug_pass_name = kGeometryPassName;
                        submission.debug_entity_name = task.debug_name.c_str();
                        if (!termin::submit_render_item_draw(*context.ctx2, *task.item, submission)) {
                            tc::Log::error("[%s] item submission failed for object %llu",
                                           kGeometryPassName,
                                           static_cast<unsigned long long>(task.item->source.object_id));
                            report.success = false;
                            continue;
                        }
                        report.rendered_items.push_back({
                            task.item->source.object_id,
                            task.item->source.generation,
                        });
                        context.capture_internal(task.debug_name.c_str(),
                                                 render_pass.colors[0].texture,
                                                 context.render_rect.width,
                                                 context.render_rect.height);
                    }
                }

                context.ctx2->end_pass();
            }

        private:
            std::vector<std::string> internal_symbols_;
        };

        class PlotScene3DChromePass final : public termin::CxxFramePass {
        public:
            PlotScene3DChromePass() {
                pass_name_set(kChromePassName);
            }

            std::set<const char*> compute_reads() const override {
                return {kGeometryResource};
            }

            std::set<const char*> compute_writes() const override {
                return {kOutputResource};
            }

            std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override {
                return {{kGeometryResource, kOutputResource}};
            }

            std::vector<std::string> get_internal_symbols() const override {
                return {"PlotScene3D grid labels"};
            }

            void execute(termin::ExecuteContext& context) override {
                const PlotScene3DRenderServices* services = require_plot_services(context, kChromePassName);
                if (!services) {
                    return;
                }
                PlotScene3DExecutionReport& report = *services->report;
                report.chrome_executed = true;

                if (!context.ctx2 || !context.render_item_snapshot || !services->font) {
                    tc::Log::error("[%s] render context, snapshot or font is missing", kChromePassName);
                    report.success = false;
                    return;
                }

                const PlotScene3DRenderItemPayload* selected_grid_payload = nullptr;
                for (std::size_t item_index = 0; item_index < context.render_item_snapshot->item_count();
                     ++item_index) {
                    const tc_render_item* item = context.render_item_snapshot->item(item_index);
                    if (item && is_selected_grid(*item, *services)) {
                        selected_grid_payload = plot_scene3d_render_item_payload(*item);
                        break;
                    }
                }
                if (!selected_grid_payload || !selected_grid_payload->item ||
                    selected_grid_payload->item->grid_style.labels_visible == 0) {
                    return;
                }

                const termin::FrameGraphColorAttachment color_attachment{
                    kOutputResource,
                    tgfx::LoadOp::Load,
                    tgfx::StoreOp::Store,
                    {0.0f, 0.0f, 0.0f, 0.0f},
                };
                const termin::FrameGraphDepthAttachment depth_attachment{
                    kOutputResource,
                    tgfx::LoadOp::Load,
                    tgfx::StoreOp::Store,
                    1.0f,
                    0,
                };
                tgfx::RenderPassDesc render_pass;
                if (!context.build_render_pass(std::span<const termin::FrameGraphColorAttachment>(&color_attachment, 1),
                                               &depth_attachment,
                                               render_pass) ||
                    !context.ctx2->begin_pass(render_pass)) {
                    tc::Log::error("[%s] failed to begin framegraph render pass", kChromePassName);
                    report.success = false;
                    return;
                }

                context.ctx2->set_viewport(0, 0, context.render_rect.width, context.render_rect.height);
                chrome_renderer_.draw_grid_labels(*context.ctx2,
                                                  *services->font,
                                                  selected_grid_payload->frame,
                                                  selected_grid_payload->item->grid_style,
                                                  context.render_rect.width,
                                                  context.render_rect.height);
                context.capture_internal("PlotScene3D grid labels",
                                         render_pass.colors[0].texture,
                                         context.render_rect.width,
                                         context.render_rect.height);
                context.ctx2->end_pass();
            }

            void release_gpu() {
                chrome_renderer_.release_gpu();
            }

        private:
            PlotScene3DChartChromeRenderer chrome_renderer_;
        };

    } // namespace

    class PlotScene3DRenderPipeline::Impl {
    public:
        explicit Impl(GpuHost& host)
            : host_(&host),
              pipeline_("RetainedChart3D") {
            if (!pipeline_.is_valid()) {
                throw std::runtime_error("failed to create RetainedChart3D render pipeline");
            }
            engine_.set_graphics_host(host.graphics());
            auto* geometry_pass = new PlotScene3DGeometryPass();
            pipeline_.add_pass(geometry_pass->tc_pass_ptr());
            chrome_pass_ = new PlotScene3DChromePass();
            pipeline_.add_pass(chrome_pass_->tc_pass_ptr());
        }

        ~Impl() {
            pipeline_.destroy();
            chrome_pass_ = nullptr;
        }

        PlotScene3DRenderResult execute(const termin::RenderItemSnapshot& snapshot,
                                        std::uint64_t selected_grid_namespace,
                                        std::uint64_t selected_grid_object,
                                        std::uint32_t selected_grid_generation,
                                        tgfx::TextureHandle color,
                                        tgfx::TextureHandle depth,
                                        int width,
                                        int height) {
            PlotScene3DExecutionReport report;
            report.rendered_items.reserve(snapshot.item_count());
            PlotScene3DRenderServices services;
            services.font = &host_->font();
            services.selected_grid_namespace = selected_grid_namespace;
            services.selected_grid_object = selected_grid_object;
            services.selected_grid_generation = selected_grid_generation;
            services.report = &report;

            termin::RenderExecutionCapabilities capabilities;
            capabilities.add(services);

            termin::RenderTargetContext target;
            target.name = "RetainedChart3DTarget";
            target.render_rect = {0, 0, width, height};
            target.output_color_tex = color;
            target.output_depth_tex = depth;
            target.output_color_format = tgfx::PixelFormat::RGBA8_UNorm;
            target.output_depth_format = tgfx::PixelFormat::D24_UNorm;
            target.clear_color_enabled = true;
            target.clear_color[0] = 0.08f;
            target.clear_color[1] = 0.09f;
            target.clear_color[2] = 0.11f;
            target.clear_color[3] = 1.0f;
            target.clear_depth_enabled = true;
            target.clear_depth = 1.0f;

            termin::RenderExecution execution;
            execution.pipeline = &pipeline_;
            execution.default_render_target = target.name;
            execution.targets.emplace(target.name,
                                      termin::RenderExecutionTarget{
                                          .context = &target,
                                          .render_items = &snapshot,
                                          .capabilities = &capabilities,
                                      });
            engine_.execute_pipeline(execution);

            PlotScene3DRenderResult result;
            result.success = report.success && report.geometry_executed && report.chrome_executed;
            result.rendered_items = std::move(report.rendered_items);
            return result;
        }

        void release_gpu() {
            if (chrome_pass_) {
                chrome_pass_->release_gpu();
            }
        }

    private:
        GpuHost* host_ = nullptr;
        termin::RenderEngine engine_;
        termin::RenderPipeline pipeline_;
        PlotScene3DChromePass* chrome_pass_ = nullptr;
    };

    PlotScene3DRenderPipeline::PlotScene3DRenderPipeline(GpuHost& host)
        : impl_(std::make_unique<Impl>(host)) {}

    PlotScene3DRenderPipeline::~PlotScene3DRenderPipeline() = default;

    PlotScene3DRenderResult PlotScene3DRenderPipeline::execute(const termin::RenderItemSnapshot& snapshot,
                                                               std::uint64_t selected_grid_namespace,
                                                               std::uint64_t selected_grid_object,
                                                               std::uint32_t selected_grid_generation,
                                                               tgfx::TextureHandle color,
                                                               tgfx::TextureHandle depth,
                                                               int width,
                                                               int height) {
        return impl_->execute(snapshot,
                              selected_grid_namespace,
                              selected_grid_object,
                              selected_grid_generation,
                              color,
                              depth,
                              width,
                              height);
    }

    void PlotScene3DRenderPipeline::release_gpu() {
        impl_->release_gpu();
    }

} // namespace tcplot
