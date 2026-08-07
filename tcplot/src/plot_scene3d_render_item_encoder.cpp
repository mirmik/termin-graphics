#include "plot_scene3d_render_item_encoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>

#include <tcbase/tc_log.hpp>
#include <termin/camera/orbit_camera.hpp>
#include <termin/render/material_pipeline.hpp>
#include <termin/render/render_context.hpp>
#include <termin/render/render_item_submission.hpp>
#include <tgfx2/builtin_shader_sources.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/axes.hpp"

extern "C"
{
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tcplot
{
    namespace
    {

        constexpr const char* kPlot3DShaderUuid = "termin-engine-tcplot-3d";
        constexpr uint32_t kPlot3DFloatsPerVertex = 19;

        struct Plot3DDrawData
        {
            float mvp[16];
            float params[4];
            float surface_color[4];
            float axis_shading[4];
            float light_strength[4];
        };
        static_assert(sizeof(Plot3DDrawData) == 128);

        tc_shader_handle plot3d_shader_handle()
        {
            static tc_shader_handle handle = tc_shader_handle_invalid();
            if (!tc_shader_is_valid(handle))
            {
                handle = tgfx::register_builtin_shader_from_catalog(
                    kPlot3DShaderUuid);
            }
            return handle;
        }

        void append_surface_vertex(std::vector<float>& vertices,
                                   const PlotScene3DItemRenderData& data,
                                   uint32_t row,
                                   uint32_t column)
        {
            const size_t index =
                static_cast<size_t>(row) * data.columns + column;
            const tc_surface_item3d_style& style = data.surface_style;
            vertices.push_back(static_cast<float>(data.x[index]));
            vertices.push_back(static_cast<float>(data.y[index]));
            vertices.push_back(static_cast<float>(data.z[index]));
            vertices.push_back(style.color_r);
            vertices.push_back(style.color_g);
            vertices.push_back(style.color_b);
            vertices.push_back(style.color_a);
            vertices.push_back(static_cast<float>(column));
            vertices.push_back(static_cast<float>(row));
            vertices.push_back(static_cast<float>(
                std::max<uint32_t>(1, style.surface_grid_col_step)));
            vertices.push_back(static_cast<float>(
                std::max<uint32_t>(1, style.surface_grid_row_step)));
            vertices.push_back(style.surface_grid_r);
            vertices.push_back(style.surface_grid_g);
            vertices.push_back(style.surface_grid_b);
            vertices.push_back(style.surface_grid_a);
            vertices.push_back(style.surface_grid_visible != 0 ? 1.0f : 0.0f);
            vertices.push_back(std::max(style.surface_grid_width_px, 0.1f));
            vertices.push_back(static_cast<float>(data.columns - 1));
            vertices.push_back(static_cast<float>(data.rows - 1));
        }

        void append_scatter_vertex(std::vector<float>& vertices,
                                   float x,
                                   float y,
                                   float z,
                                   const tc_scatter_item3d_style& style)
        {
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(style.color_r);
            vertices.push_back(style.color_g);
            vertices.push_back(style.color_b);
            vertices.push_back(style.color_a);
            for (uint32_t index = 0; index < 12; ++index)
            {
                vertices.push_back(0.0f);
            }
        }

        void append_line_vertex(std::vector<float>& vertices,
                                float x,
                                float y,
                                float z,
                                float r,
                                float g,
                                float b,
                                float a)
        {
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(r);
            vertices.push_back(g);
            vertices.push_back(b);
            vertices.push_back(a);
            for (uint32_t index = 0; index < 12; ++index)
            {
                vertices.push_back(0.0f);
            }
        }

        bool is_encoded_plot_item_kind(uint32_t kind)
        {
            return kind == PLOT_RENDER_ITEM_KIND_SURFACE ||
                   kind == PLOT_RENDER_ITEM_KIND_SCATTER ||
                   kind == PLOT_RENDER_ITEM_KIND_GRID ||
                   kind == PLOT_RENDER_ITEM_KIND_LINE;
        }

        bool
        payload_kind_matches_item(const tc_render_item& item,
                                  const PlotScene3DRenderItemPayload& payload)
        {
            if (!payload.item)
            {
                return false;
            }
            return (item.kind == PLOT_RENDER_ITEM_KIND_SURFACE &&
                    payload.item->kind == TC_PLOT_ITEM3D_SURFACE) ||
                   (item.kind == PLOT_RENDER_ITEM_KIND_SCATTER &&
                    payload.item->kind == TC_PLOT_ITEM3D_SCATTER) ||
                   (item.kind == PLOT_RENDER_ITEM_KIND_GRID &&
                    payload.item->kind == TC_PLOT_ITEM3D_GRID) ||
                   (item.kind == PLOT_RENDER_ITEM_KIND_LINE &&
                    payload.item->kind == TC_PLOT_ITEM3D_LINE);
        }

        termin::RenderItemTaskRejection plan_plot_scene3d_shader(
            const termin::RenderItemTaskPlanningRequest& request,
            termin::RenderItemTaskShaderPlan& out_plan,
            const char*& out_detail,
            void*)
        {
            const PlotScene3DRenderItemPayload* payload =
                request.item ? plot_scene3d_render_item_payload(*request.item)
                             : nullptr;
            if (!request.item ||
                !is_encoded_plot_item_kind(request.item->kind) || !payload ||
                !payload_kind_matches_item(*request.item, *payload))
            {
                out_detail =
                    "item has no matching immutable PlotScene3D payload";
                return termin::RenderItemTaskRejection::ShaderPlanningRejected;
            }
            if (request.material_phase)
            {
                out_detail = "PlotScene3D items do not accept material phases";
                return termin::RenderItemTaskRejection::MaterialPhaseForbidden;
            }
            const tc_shader_handle shader = plot3d_shader_handle();
            if (tc_shader_handle_is_invalid(shader))
            {
                out_detail = "failed to register the builtin tcplot3d shader";
                return termin::RenderItemTaskRejection::ShaderPlanningRejected;
            }
            out_plan.final_shader = shader;
            if (!out_plan.add_shader_usage(shader))
            {
                out_detail = "PlotScene3D shader usage packet is full";
                return termin::RenderItemTaskRejection::ShaderPlanningRejected;
            }
            out_detail = nullptr;
            return termin::RenderItemTaskRejection::None;
        }

        bool prepare_plot_scene3d_draw(
            tgfx::RenderContext2& context,
            const termin::RenderItemDrawSubmitRequest& request,
            const PlotScene3DRenderItemPayload& payload,
            const PlotScene3DItemRenderData& data,
            bool surface_mode,
            const char* pass_name,
            tgfx::VertexLayoutDesc& out_layout)
        {
            termin::MaterialPipelineShaderBinding shader_binding{};
            if (!termin::ensure_material_pipeline_shader(context,
                                                         *request.device,
                                                         request.shader_handle,
                                                         pass_name,
                                                         shader_binding))
            {
                tc::Log::error("[%s] failed to prepare PlotScene3D shader",
                               pass_name);
                return false;
            }

            termin::OrbitCamera camera;
            const tc_orbit_camera3d_state& camera_state = payload.frame.camera;
            camera.target = {
                camera_state.target_x,
                camera_state.target_y,
                camera_state.target_z,
            };
            camera.distance = camera_state.distance;
            camera.azimuth = camera_state.azimuth;
            camera.elevation = camera_state.elevation;
            camera.fov_y = camera_state.fov_y;
            camera.near_clip = camera_state.near_clip;
            camera.far_clip = camera_state.far_clip;
            const float aspect =
                static_cast<float>(request.draw_context->viewport_width) /
                static_cast<float>(request.draw_context->viewport_height);
            const termin::Mat44f mvp =
                camera.projection_matrix(aspect) * camera.view_matrix();

            Plot3DDrawData draw{};
            std::memcpy(draw.mvp, mvp.data, sizeof(draw.mvp));
            for (int row = 0; row < 4; ++row)
            {
                draw.mvp[0 * 4 + row] *= payload.frame.axis_scale[0];
                draw.mvp[1 * 4 + row] *= payload.frame.axis_scale[1];
                draw.mvp[2 * 4 + row] *= payload.frame.axis_scale[2];
            }
            draw.params[0] = static_cast<float>(payload.frame.bounds_min[2]);
            draw.params[1] = static_cast<float>(payload.frame.bounds_max[2]);
            draw.axis_shading[0] = payload.frame.axis_scale[0];
            draw.axis_shading[1] = payload.frame.axis_scale[1];
            draw.axis_shading[2] = payload.frame.axis_scale[2];
            if (surface_mode)
            {
                const tc_surface_item3d_style& style = data.surface_style;
                draw.params[2] = style.wireframe == 0 ? 1.0f : 0.0f;
                draw.params[3] = static_cast<float>(style.colormap) +
                                 (style.colormap_reversed != 0 ? 100.0f : 0.0f);
                draw.surface_color[0] = style.color_r;
                draw.surface_color[1] = style.color_g;
                draw.surface_color[2] = style.color_b;
                draw.surface_color[3] = style.color_a;
                draw.axis_shading[3] =
                    style.wireframe == 0 && payload.frame.surface_shading
                        ? 1.0f
                        : 0.0f;
                draw.light_strength[0] =
                    payload.frame.surface_light_direction[0];
                draw.light_strength[1] =
                    payload.frame.surface_light_direction[1];
                draw.light_strength[2] =
                    payload.frame.surface_light_direction[2];
                draw.light_strength[3] = std::clamp(
                    payload.frame.surface_shading_strength, 0.0f, 1.0f);
            }
            context.bind_uniform_data(
                "tcplot3d_draw", &draw, static_cast<uint32_t>(sizeof(draw)));

            out_layout = {};
            out_layout.stride = kPlot3DFloatsPerVertex * sizeof(float);
            out_layout.use_shader_input_locations = true;
            out_layout.attribute_count = 5;
            out_layout.attributes[0] = {
                0, tgfx::VertexFormat::Float3, 0, nullptr};
            out_layout.attributes[1] = {
                1, tgfx::VertexFormat::Float4, 3 * sizeof(float), nullptr};
            out_layout.attributes[2] = {
                2, tgfx::VertexFormat::Float4, 7 * sizeof(float), nullptr};
            out_layout.attributes[3] = {
                3, tgfx::VertexFormat::Float4, 11 * sizeof(float), nullptr};
            out_layout.attributes[4] = {
                4, tgfx::VertexFormat::Float4, 15 * sizeof(float), nullptr};
            return true;
        }

        bool encode_surface(tgfx::RenderContext2& context,
                            const tc_render_item& item,
                            const termin::RenderItemDrawSubmitRequest& request,
                            void*)
        {
            const char* pass_name = request.debug_pass_name
                                        ? request.debug_pass_name
                                        : "PlotScene3DSurface";
            if (item.kind != PLOT_RENDER_ITEM_KIND_SURFACE)
            {
                tc::Log::error(
                    "[%s] PlotScene3D surface encoder received item kind %u",
                    pass_name,
                    item.kind);
                return false;
            }
            if (request.phase != TC_PHASE_OPAQUE || request.material_phase)
            {
                tc::Log::error("[%s] PlotScene3D surface encoder requires "
                               "opaque no-material submission",
                               pass_name);
                return false;
            }
            if (!request.device || !request.draw_context ||
                request.draw_context->viewport_width <= 0 ||
                request.draw_context->viewport_height <= 0)
            {
                tc::Log::error("[%s] PlotScene3D surface encoder requires a "
                               "device and sized draw context",
                               pass_name);
                return false;
            }
            const PlotScene3DRenderItemPayload* payload =
                plot_scene3d_render_item_payload(item);
            if (!payload || !payload->item ||
                payload->item->kind != TC_PLOT_ITEM3D_SURFACE)
            {
                tc::Log::error("[%s] PlotScene3D surface encoder received a "
                               "malformed payload",
                               pass_name);
                return false;
            }
            const PlotScene3DItemRenderData& data = *payload->item;
            if (data.draw_vertex_count == 0 ||
                data.draw_vertices.size() !=
                    static_cast<size_t>(data.draw_vertex_count) *
                        kPlot3DFloatsPerVertex ||
                data.draw_vertices.size() >
                    std::numeric_limits<uint32_t>::max() / sizeof(float))
            {
                tc::Log::error("[%s] PlotScene3D surface encoder received an "
                               "invalid draw stream",
                               pass_name);
                return false;
            }

            const tc_surface_item3d_style& style = data.surface_style;
            tgfx::VertexLayoutDesc layout{};
            if (!prepare_plot_scene3d_draw(
                    context, request, *payload, data, true, pass_name, layout))
            {
                return false;
            }

            context.set_cull(tgfx::CullMode::None);
            context.set_depth_write(style.wireframe == 0);
            context.set_depth_test(style.wireframe == 0);
            context.set_depth_func(tgfx::CompareOp::Less);
            // Preserve PlotEngine3D's current contract: filled surfaces are
            // opaque; wireframe lines blend over the already rendered chart
            // geometry.
            context.set_blend(style.wireframe != 0);
            context.draw_transient_arrays(
                data.draw_vertices.data(),
                static_cast<uint32_t>(data.draw_vertices.size() *
                                      sizeof(float)),
                data.draw_vertex_count,
                layout,
                style.wireframe != 0 ? tgfx::PrimitiveTopology::LineList
                                     : tgfx::PrimitiveTopology::TriangleList);
            return true;
        }

        bool encode_scatter(tgfx::RenderContext2& context,
                            const tc_render_item& item,
                            const termin::RenderItemDrawSubmitRequest& request,
                            void*)
        {
            const char* pass_name = request.debug_pass_name
                                        ? request.debug_pass_name
                                        : "PlotScene3DScatter";
            if (item.kind != PLOT_RENDER_ITEM_KIND_SCATTER)
            {
                tc::Log::error(
                    "[%s] PlotScene3D scatter encoder received item kind %u",
                    pass_name,
                    item.kind);
                return false;
            }
            if (request.phase != TC_PHASE_OPAQUE || request.material_phase)
            {
                tc::Log::error("[%s] PlotScene3D scatter encoder requires "
                               "opaque no-material submission",
                               pass_name);
                return false;
            }
            if (!request.device || !request.draw_context ||
                request.draw_context->viewport_width <= 0 ||
                request.draw_context->viewport_height <= 0)
            {
                tc::Log::error("[%s] PlotScene3D scatter encoder requires a "
                               "device and sized draw context",
                               pass_name);
                return false;
            }
            const PlotScene3DRenderItemPayload* payload =
                plot_scene3d_render_item_payload(item);
            if (!payload || !payload->item ||
                payload->item->kind != TC_PLOT_ITEM3D_SCATTER)
            {
                tc::Log::error("[%s] PlotScene3D scatter encoder received a "
                               "malformed payload",
                               pass_name);
                return false;
            }
            const PlotScene3DItemRenderData& data = *payload->item;
            if (data.draw_vertex_count == 0 ||
                data.draw_vertices.size() !=
                    static_cast<size_t>(data.draw_vertex_count) *
                        kPlot3DFloatsPerVertex ||
                data.draw_vertices.size() >
                    std::numeric_limits<uint32_t>::max() / sizeof(float))
            {
                tc::Log::error("[%s] PlotScene3D scatter encoder received an "
                               "invalid draw stream",
                               pass_name);
                return false;
            }

            tgfx::VertexLayoutDesc layout{};
            if (!prepare_plot_scene3d_draw(
                    context, request, *payload, data, false, pass_name, layout))
            {
                return false;
            }

            context.set_cull(tgfx::CullMode::None);
            context.set_depth_write(true);
            context.set_depth_test(true);
            context.set_depth_func(tgfx::CompareOp::Less);
            context.set_blend(true);
            context.draw_transient_arrays(
                data.draw_vertices.data(),
                static_cast<uint32_t>(data.draw_vertices.size() *
                                      sizeof(float)),
                data.draw_vertex_count,
                layout,
                tgfx::PrimitiveTopology::LineList);
            return true;
        }

        bool encode_grid(tgfx::RenderContext2& context,
                         const tc_render_item& item,
                         const termin::RenderItemDrawSubmitRequest& request,
                         void*)
        {
            const char* pass_name = request.debug_pass_name
                                        ? request.debug_pass_name
                                        : "PlotScene3DGrid";
            if (item.kind != PLOT_RENDER_ITEM_KIND_GRID)
            {
                tc::Log::error(
                    "[%s] PlotScene3D grid encoder received item kind %u",
                    pass_name,
                    item.kind);
                return false;
            }
            if (request.phase != TC_PHASE_OPAQUE || request.material_phase)
            {
                tc::Log::error("[%s] PlotScene3D grid encoder requires "
                               "opaque no-material submission",
                               pass_name);
                return false;
            }
            if (!request.device || !request.draw_context ||
                request.draw_context->viewport_width <= 0 ||
                request.draw_context->viewport_height <= 0)
            {
                tc::Log::error("[%s] PlotScene3D grid encoder requires a "
                               "device and sized draw context",
                               pass_name);
                return false;
            }
            const PlotScene3DRenderItemPayload* payload =
                plot_scene3d_render_item_payload(item);
            if (!payload || !payload->item ||
                payload->item->kind != TC_PLOT_ITEM3D_GRID)
            {
                tc::Log::error("[%s] PlotScene3D grid encoder received a "
                               "malformed payload",
                               pass_name);
                return false;
            }
            const PlotScene3DItemRenderData& data = *payload->item;
            if (data.draw_vertex_count == 0 ||
                data.draw_vertices.size() !=
                    static_cast<size_t>(data.draw_vertex_count) *
                        kPlot3DFloatsPerVertex ||
                data.draw_vertices.size() >
                    std::numeric_limits<uint32_t>::max() / sizeof(float))
            {
                tc::Log::error("[%s] PlotScene3D grid encoder received an "
                               "invalid draw stream",
                               pass_name);
                return false;
            }

            tgfx::VertexLayoutDesc layout{};
            if (!prepare_plot_scene3d_draw(
                    context, request, *payload, data, false, pass_name, layout))
            {
                return false;
            }

            context.set_cull(tgfx::CullMode::None);
            context.set_depth_write(true);
            context.set_depth_test(true);
            context.set_depth_func(tgfx::CompareOp::Less);
            context.set_blend(true);
            context.draw_transient_arrays(
                data.draw_vertices.data(),
                static_cast<uint32_t>(data.draw_vertices.size() *
                                      sizeof(float)),
                data.draw_vertex_count,
                layout,
                tgfx::PrimitiveTopology::LineList);
            return true;
        }

        bool encode_line(tgfx::RenderContext2& context,
                         const tc_render_item& item,
                         const termin::RenderItemDrawSubmitRequest& request,
                         void*)
        {
            const char* pass_name = request.debug_pass_name
                                        ? request.debug_pass_name
                                        : "PlotScene3DLine";
            if (item.kind != PLOT_RENDER_ITEM_KIND_LINE ||
                request.phase != TC_PHASE_OPAQUE || request.material_phase ||
                !request.device || !request.draw_context ||
                request.draw_context->viewport_width <= 0 ||
                request.draw_context->viewport_height <= 0)
            {
                tc::Log::error("[%s] invalid retained line submission",
                               pass_name);
                return false;
            }
            const PlotScene3DRenderItemPayload* payload =
                plot_scene3d_render_item_payload(item);
            if (!payload || !payload->item ||
                payload->item->kind != TC_PLOT_ITEM3D_LINE)
            {
                tc::Log::error("[%s] malformed retained line payload",
                               pass_name);
                return false;
            }
            const PlotScene3DItemRenderData& data = *payload->item;
            if (data.draw_vertex_count == 0 ||
                data.draw_vertices.size() !=
                    static_cast<size_t>(data.draw_vertex_count) *
                        kPlot3DFloatsPerVertex ||
                data.draw_vertices.size() >
                    std::numeric_limits<uint32_t>::max() / sizeof(float))
            {
                tc::Log::error("[%s] invalid retained line draw stream",
                               pass_name);
                return false;
            }
            tgfx::VertexLayoutDesc layout{};
            if (!prepare_plot_scene3d_draw(
                    context, request, *payload, data, false, pass_name, layout))
            {
                return false;
            }
            context.set_cull(tgfx::CullMode::None);
            context.set_depth_write(true);
            context.set_depth_test(true);
            context.set_depth_func(tgfx::CompareOp::Less);
            context.set_blend(true);
            context.draw_transient_arrays(
                data.draw_vertices.data(),
                static_cast<uint32_t>(data.draw_vertices.size() *
                                      sizeof(float)),
                data.draw_vertex_count,
                layout,
                tgfx::PrimitiveTopology::LineList);
            return true;
        }

    } // namespace

    void build_plot_scene3d_surface_draw_stream(PlotScene3DItemRenderData& data)
    {
        data.draw_vertices.clear();
        data.draw_vertex_count = 0;
        if (data.kind != TC_PLOT_ITEM3D_SURFACE || data.rows < 2 ||
            data.columns < 2)
        {
            return;
        }
        const size_t point_count =
            static_cast<size_t>(data.rows) * data.columns;
        if (data.x.size() != point_count || data.y.size() != point_count ||
            data.z.size() != point_count)
        {
            tc::Log::error("[PlotScene3DSurface] cannot build draw stream from "
                           "mismatched data arrays");
            return;
        }

        const size_t vertices_per_cell =
            data.surface_style.wireframe != 0 ? 12u : 6u;
        const size_t cell_count =
            static_cast<size_t>(data.rows - 1) * (data.columns - 1);
        const size_t max_float_count =
            std::numeric_limits<uint32_t>::max() / sizeof(float);
        if (cell_count >
            max_float_count / (vertices_per_cell * kPlot3DFloatsPerVertex))
        {
            tc::Log::error("[PlotScene3DSurface] draw stream exceeds the "
                           "transient upload ABI");
            return;
        }
        data.draw_vertices.reserve(cell_count * vertices_per_cell *
                                   kPlot3DFloatsPerVertex);
        for (uint32_t row = 0; row + 1 < data.rows; ++row)
        {
            for (uint32_t column = 0; column + 1 < data.columns; ++column)
            {
                if (data.surface_style.wireframe != 0)
                {
                    const uint32_t edge_vertices[12][2] = {
                        {row, column},
                        {row, column + 1},
                        {row, column + 1},
                        {row + 1, column},
                        {row + 1, column},
                        {row, column},
                        {row, column + 1},
                        {row + 1, column + 1},
                        {row + 1, column + 1},
                        {row + 1, column},
                        {row + 1, column},
                        {row, column + 1},
                    };
                    for (const auto& vertex : edge_vertices)
                    {
                        append_surface_vertex(
                            data.draw_vertices, data, vertex[0], vertex[1]);
                    }
                }
                else
                {
                    const uint32_t triangle_vertices[6][2] = {
                        {row, column},
                        {row, column + 1},
                        {row + 1, column},
                        {row, column + 1},
                        {row + 1, column + 1},
                        {row + 1, column},
                    };
                    for (const auto& vertex : triangle_vertices)
                    {
                        append_surface_vertex(
                            data.draw_vertices, data, vertex[0], vertex[1]);
                    }
                }
            }
        }
        data.draw_vertex_count = static_cast<uint32_t>(
            data.draw_vertices.size() / kPlot3DFloatsPerVertex);
    }

    void build_plot_scene3d_scatter_draw_stream(PlotScene3DItemRenderData& data)
    {
        data.draw_vertices.clear();
        data.draw_vertex_count = 0;
        if (data.kind != TC_PLOT_ITEM3D_SCATTER || data.x.empty() ||
            data.x.size() != data.y.size() || data.x.size() != data.z.size())
        {
            tc::Log::error("[PlotScene3DScatter] cannot build draw stream from "
                           "mismatched data arrays");
            return;
        }

        constexpr size_t kVerticesPerPoint = 6;
        const size_t max_float_count =
            std::numeric_limits<uint32_t>::max() / sizeof(float);
        if (data.x.size() >
            max_float_count / (kVerticesPerPoint * kPlot3DFloatsPerVertex))
        {
            tc::Log::error("[PlotScene3DScatter] draw stream exceeds the "
                           "transient upload ABI");
            return;
        }

        double minimum[3] = {data.x[0], data.y[0], data.z[0]};
        double maximum[3] = {data.x[0], data.y[0], data.z[0]};
        for (size_t index = 1; index < data.x.size(); ++index)
        {
            const double point[3] = {
                data.x[index], data.y[index], data.z[index]};
            for (size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], point[axis]);
                maximum[axis] = std::max(maximum[axis], point[axis]);
            }
        }
        const double dx = maximum[0] - minimum[0];
        const double dy = maximum[1] - minimum[1];
        const double dz = maximum[2] - minimum[2];
        const float cross_size = static_cast<float>(
            std::sqrt(dx * dx + dy * dy + dz * dz) * 0.008 *
            (std::max(data.scatter_style.size, 0.1f) / 4.0f));

        data.draw_vertices.reserve(data.x.size() * kVerticesPerPoint *
                                   kPlot3DFloatsPerVertex);
        for (size_t index = 0; index < data.x.size(); ++index)
        {
            const float x = static_cast<float>(data.x[index]);
            const float y = static_cast<float>(data.y[index]);
            const float z = static_cast<float>(data.z[index]);
            append_scatter_vertex(
                data.draw_vertices, x - cross_size, y, z, data.scatter_style);
            append_scatter_vertex(
                data.draw_vertices, x + cross_size, y, z, data.scatter_style);
            append_scatter_vertex(
                data.draw_vertices, x, y - cross_size, z, data.scatter_style);
            append_scatter_vertex(
                data.draw_vertices, x, y + cross_size, z, data.scatter_style);
            append_scatter_vertex(
                data.draw_vertices, x, y, z - cross_size, data.scatter_style);
            append_scatter_vertex(
                data.draw_vertices, x, y, z + cross_size, data.scatter_style);
        }
        data.draw_vertex_count = static_cast<uint32_t>(
            data.draw_vertices.size() / kPlot3DFloatsPerVertex);
    }

    void build_plot_scene3d_line_draw_stream(PlotScene3DItemRenderData& data)
    {
        data.draw_vertices.clear();
        data.draw_vertex_count = 0;
        if (data.kind != TC_PLOT_ITEM3D_LINE || data.x.size() < 2 ||
            data.x.size() != data.y.size() || data.x.size() != data.z.size())
        {
            tc::Log::error("[PlotScene3DLine] cannot build draw stream from "
                           "mismatched data arrays");
            return;
        }
        constexpr size_t kVerticesPerSegment = 2;
        const size_t segment_count = data.x.size() - 1;
        const size_t max_float_count =
            std::numeric_limits<uint32_t>::max() / sizeof(float);
        if (segment_count >
            max_float_count /
                (kVerticesPerSegment * kPlot3DFloatsPerVertex))
        {
            tc::Log::error("[PlotScene3DLine] draw stream exceeds the "
                           "transient upload ABI");
            return;
        }
        const tc_line_item3d_style& style = data.line_style;
        data.draw_vertices.reserve(segment_count * kVerticesPerSegment *
                                   kPlot3DFloatsPerVertex);
        for (size_t index = 0; index < segment_count; ++index)
        {
            for (size_t endpoint = index; endpoint <= index + 1; ++endpoint)
            {
                append_line_vertex(
                    data.draw_vertices,
                    static_cast<float>(data.x[endpoint]),
                    static_cast<float>(data.y[endpoint]),
                    static_cast<float>(data.z[endpoint]),
                    style.color_r,
                    style.color_g,
                    style.color_b,
                    style.color_a);
            }
        }
        data.draw_vertex_count = static_cast<uint32_t>(
            data.draw_vertices.size() / kPlot3DFloatsPerVertex);
    }

    void build_plot_scene3d_grid_draw_stream(
        PlotScene3DItemRenderData& data,
        const PlotScene3DFrameRenderState& frame)
    {
        data.draw_vertices.clear();
        data.draw_vertex_count = 0;
        if (data.kind != TC_PLOT_ITEM3D_GRID)
        {
            tc::Log::error("[PlotScene3DGrid] cannot build a grid draw stream "
                           "for a non-grid item");
            return;
        }

        const tc_grid_item3d_style& style = data.grid_style;
        constexpr size_t kAxisCount = 3;
        constexpr size_t kVerticesPerLine = 2;
        constexpr size_t kMaximumTickCount = 8;
        data.draw_vertices.reserve(
            (kAxisCount * kMaximumTickCount + kAxisCount) *
            kVerticesPerLine * kPlot3DFloatsPerVertex);

        for (size_t axis = 0; axis < kAxisCount; ++axis)
        {
            size_t other_axes[2]{};
            size_t other_index = 0;
            for (size_t candidate = 0; candidate < kAxisCount; ++candidate)
            {
                if (candidate != axis)
                {
                    other_axes[other_index++] = candidate;
                }
            }
            for (double tick : axes::nice_ticks(
                     frame.bounds_min[axis], frame.bounds_max[axis], 8))
            {
                std::array<double, 3> start{};
                std::array<double, 3> end{};
                start[axis] = tick;
                end[axis] = tick;
                start[other_axes[0]] = frame.bounds_min[other_axes[0]];
                end[other_axes[0]] = frame.bounds_max[other_axes[0]];
                start[other_axes[1]] = frame.bounds_min[other_axes[1]];
                end[other_axes[1]] = frame.bounds_min[other_axes[1]];
                append_line_vertex(
                    data.draw_vertices,
                    static_cast<float>(start[0]),
                    static_cast<float>(start[1]),
                    static_cast<float>(start[2]),
                    style.grid_r,
                    style.grid_g,
                    style.grid_b,
                    style.grid_a);
                append_line_vertex(
                    data.draw_vertices,
                    static_cast<float>(end[0]),
                    static_cast<float>(end[1]),
                    static_cast<float>(end[2]),
                    style.grid_r,
                    style.grid_g,
                    style.grid_b,
                    style.grid_a);
            }
        }

        const float axis_colors[3][3] = {
            {style.x_axis_r, style.x_axis_g, style.x_axis_b},
            {style.y_axis_r, style.y_axis_g, style.y_axis_b},
            {style.z_axis_r, style.z_axis_g, style.z_axis_b},
        };
        for (size_t axis = 0; axis < kAxisCount; ++axis)
        {
            std::array<double, 3> start = frame.bounds_min;
            std::array<double, 3> end = start;
            end[axis] = frame.bounds_max[axis];
            append_line_vertex(
                data.draw_vertices,
                static_cast<float>(start[0]),
                static_cast<float>(start[1]),
                static_cast<float>(start[2]),
                axis_colors[axis][0],
                axis_colors[axis][1],
                axis_colors[axis][2],
                1.0f);
            append_line_vertex(
                data.draw_vertices,
                static_cast<float>(end[0]),
                static_cast<float>(end[1]),
                static_cast<float>(end[2]),
                axis_colors[axis][0],
                axis_colors[axis][1],
                axis_colors[axis][2],
                1.0f);
        }
        data.draw_vertex_count = static_cast<uint32_t>(
            data.draw_vertices.size() / kPlot3DFloatsPerVertex);
    }

    void ensure_plot_scene3d_render_item_encoders_registered()
    {
        static std::once_flag once;
        std::call_once(once,
                       []
                       {
                           termin::RenderItemEncoderCapabilities capabilities{};
                           capabilities.phase_mask = TC_PHASE_OPAQUE;
                           capabilities.supported_task_input_mask =
                               termin::render_item_task_input_bit(
                                   termin::RenderItemTaskInput::DrawContext);
                           capabilities.required_task_input_mask =
                               termin::render_item_task_input_bit(
                                   termin::RenderItemTaskInput::DrawContext);
                           capabilities.requires_draw_context = true;
                           capabilities.consumes_common_resources = false;

                           termin::RenderItemDrawEncoderDesc descriptor{};
                           descriptor.encode = encode_surface;
                           descriptor.plan_task_shader =
                               plan_plot_scene3d_shader;
                           descriptor.debug_name = "PlotScene3DSurface";
                           descriptor.capabilities = capabilities;
                           if (!termin::register_render_item_draw_encoder(
                                   PLOT_RENDER_ITEM_KIND_SURFACE, descriptor))
                           {
                               tc::Log::error("[PlotScene3DSurface] failed to "
                                              "register RenderItem encoder");
                           }

                           descriptor.encode = encode_scatter;
                           descriptor.debug_name = "PlotScene3DScatter";
                           if (!termin::register_render_item_draw_encoder(
                                   PLOT_RENDER_ITEM_KIND_SCATTER, descriptor))
                           {
                               tc::Log::error("[PlotScene3DScatter] failed to "
                                              "register RenderItem encoder");
                           }

                           descriptor.encode = encode_grid;
                           descriptor.debug_name = "PlotScene3DGrid";
                           if (!termin::register_render_item_draw_encoder(
                                   PLOT_RENDER_ITEM_KIND_GRID, descriptor))
                           {
                               tc::Log::error("[PlotScene3DGrid] failed to "
                                              "register RenderItem encoder");
                           }

                           descriptor.encode = encode_line;
                           descriptor.debug_name = "PlotScene3DLine";
                           if (!termin::register_render_item_draw_encoder(
                                   PLOT_RENDER_ITEM_KIND_LINE, descriptor))
                           {
                               tc::Log::error("[PlotScene3DLine] failed to "
                                              "register RenderItem encoder");
                           }
                       });
    }

} // namespace tcplot
