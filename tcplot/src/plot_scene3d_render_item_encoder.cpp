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

extern "C"
{
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tcplot
{
    namespace
    {

        constexpr const char* kPlot3DShaderUuid = "termin-engine-tcplot-3d";
        constexpr uint32_t kSurfaceFloatsPerVertex = 19;

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

        termin::RenderItemTaskRejection plan_surface_shader(
            const termin::RenderItemTaskPlanningRequest& request,
            termin::RenderItemTaskShaderPlan& out_plan,
            const char*& out_detail,
            void*)
        {
            if (!request.item ||
                request.item->kind != PLOT_RENDER_ITEM_KIND_SURFACE ||
                !plot_scene3d_render_item_payload(*request.item))
            {
                out_detail =
                    "surface item has no immutable PlotScene3D payload";
                return termin::RenderItemTaskRejection::ShaderPlanningRejected;
            }
            if (request.material_phase)
            {
                out_detail =
                    "PlotScene3D surface items do not accept material phases";
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
                out_detail = "surface shader usage packet is full";
                return termin::RenderItemTaskRejection::ShaderPlanningRejected;
            }
            out_detail = nullptr;
            return termin::RenderItemTaskRejection::None;
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
            if (data.surface_draw_vertex_count == 0 ||
                data.surface_draw_vertices.size() !=
                    static_cast<size_t>(data.surface_draw_vertex_count) *
                        kSurfaceFloatsPerVertex ||
                data.surface_draw_vertices.size() >
                    std::numeric_limits<uint32_t>::max() / sizeof(float))
            {
                tc::Log::error("[%s] PlotScene3D surface encoder received an "
                               "invalid draw stream",
                               pass_name);
                return false;
            }

            termin::MaterialPipelineShaderBinding shader_binding{};
            if (!termin::ensure_material_pipeline_shader(context,
                                                         *request.device,
                                                         request.shader_handle,
                                                         pass_name,
                                                         shader_binding))
            {
                tc::Log::error(
                    "[%s] failed to prepare PlotScene3D surface shader",
                    pass_name);
                return false;
            }

            termin::OrbitCamera camera;
            const tc_orbit_camera3d_state& camera_state = payload->frame.camera;
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
                draw.mvp[0 * 4 + row] *= payload->frame.axis_scale[0];
                draw.mvp[1 * 4 + row] *= payload->frame.axis_scale[1];
                draw.mvp[2 * 4 + row] *= payload->frame.axis_scale[2];
            }
            const tc_surface_item3d_style& style = data.surface_style;
            draw.params[0] = static_cast<float>(payload->frame.bounds_min[2]);
            draw.params[1] = static_cast<float>(payload->frame.bounds_max[2]);
            draw.params[2] = style.wireframe == 0 ? 1.0f : 0.0f;
            draw.params[3] = static_cast<float>(style.colormap) +
                             (style.colormap_reversed != 0 ? 100.0f : 0.0f);
            draw.surface_color[0] = style.color_r;
            draw.surface_color[1] = style.color_g;
            draw.surface_color[2] = style.color_b;
            draw.surface_color[3] = style.color_a;
            draw.axis_shading[0] = payload->frame.axis_scale[0];
            draw.axis_shading[1] = payload->frame.axis_scale[1];
            draw.axis_shading[2] = payload->frame.axis_scale[2];
            draw.axis_shading[3] =
                style.wireframe == 0 && payload->frame.surface_shading ? 1.0f
                                                                       : 0.0f;
            draw.light_strength[0] = payload->frame.surface_light_direction[0];
            draw.light_strength[1] = payload->frame.surface_light_direction[1];
            draw.light_strength[2] = payload->frame.surface_light_direction[2];
            draw.light_strength[3] =
                std::clamp(payload->frame.surface_shading_strength, 0.0f, 1.0f);
            context.bind_uniform_data(
                "tcplot3d_draw", &draw, static_cast<uint32_t>(sizeof(draw)));

            tgfx::VertexLayoutDesc layout{};
            layout.stride = kSurfaceFloatsPerVertex * sizeof(float);
            layout.use_shader_input_locations = true;
            layout.attribute_count = 5;
            layout.attributes[0] = {0, tgfx::VertexFormat::Float3, 0, nullptr};
            layout.attributes[1] = {
                1, tgfx::VertexFormat::Float4, 3 * sizeof(float), nullptr};
            layout.attributes[2] = {
                2, tgfx::VertexFormat::Float4, 7 * sizeof(float), nullptr};
            layout.attributes[3] = {
                3, tgfx::VertexFormat::Float4, 11 * sizeof(float), nullptr};
            layout.attributes[4] = {
                4, tgfx::VertexFormat::Float4, 15 * sizeof(float), nullptr};

            context.set_cull(tgfx::CullMode::None);
            context.set_depth_write(style.wireframe == 0);
            context.set_depth_test(style.wireframe == 0);
            context.set_depth_func(tgfx::CompareOp::Less);
            // Preserve PlotEngine3D's current contract: filled surfaces are
            // opaque; wireframe lines blend over the already rendered chart
            // geometry.
            context.set_blend(style.wireframe != 0);
            context.draw_transient_arrays(
                data.surface_draw_vertices.data(),
                static_cast<uint32_t>(data.surface_draw_vertices.size() *
                                      sizeof(float)),
                data.surface_draw_vertex_count,
                layout,
                style.wireframe != 0 ? tgfx::PrimitiveTopology::LineList
                                     : tgfx::PrimitiveTopology::TriangleList);
            return true;
        }

    } // namespace

    void build_plot_scene3d_surface_draw_stream(PlotScene3DItemRenderData& data)
    {
        data.surface_draw_vertices.clear();
        data.surface_draw_vertex_count = 0;
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
            max_float_count / (vertices_per_cell * kSurfaceFloatsPerVertex))
        {
            tc::Log::error("[PlotScene3DSurface] draw stream exceeds the "
                           "transient upload ABI");
            return;
        }
        data.surface_draw_vertices.reserve(cell_count * vertices_per_cell *
                                           kSurfaceFloatsPerVertex);
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
                        append_surface_vertex(data.surface_draw_vertices,
                                              data,
                                              vertex[0],
                                              vertex[1]);
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
                        append_surface_vertex(data.surface_draw_vertices,
                                              data,
                                              vertex[0],
                                              vertex[1]);
                    }
                }
            }
        }
        data.surface_draw_vertex_count = static_cast<uint32_t>(
            data.surface_draw_vertices.size() / kSurfaceFloatsPerVertex);
    }

    void ensure_plot_scene3d_surface_encoder_registered()
    {
        static std::once_flag once;
        std::call_once(once,
                       []
                       {
                           termin::RenderItemDrawEncoderDesc descriptor{};
                           descriptor.encode = encode_surface;
                           descriptor.plan_task_shader = plan_surface_shader;
                           descriptor.debug_name = "PlotScene3DSurface";
                           descriptor.capabilities.phase_mask = TC_PHASE_OPAQUE;
                           descriptor.capabilities.supported_task_input_mask =
                               termin::render_item_task_input_bit(
                                   termin::RenderItemTaskInput::DrawContext);
                           descriptor.capabilities.required_task_input_mask =
                               termin::render_item_task_input_bit(
                                   termin::RenderItemTaskInput::DrawContext);
                           descriptor.capabilities.requires_draw_context = true;
                           descriptor.capabilities.consumes_common_resources =
                               false;
                           if (!termin::register_render_item_draw_encoder(
                                   PLOT_RENDER_ITEM_KIND_SURFACE, descriptor))
                           {
                               tc::Log::error("[PlotScene3DSurface] failed to "
                                              "register RenderItem encoder");
                           }
                       });
    }

} // namespace tcplot
