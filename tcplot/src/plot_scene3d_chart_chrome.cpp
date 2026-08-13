#include "plot_scene3d_chart_chrome.hpp"

#include <algorithm>
#include <cmath>

#include <termin/camera/orbit_camera.hpp>
#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/text2d_renderer.hpp>

#include "tcplot/axes.hpp"
#include "tcplot/styles.hpp"

namespace tcplot {

    namespace {

        termin::OrbitCamera make_camera(const tc_orbit_camera3d_state& state) {
            termin::OrbitCamera camera;
            camera.target = {state.target_x, state.target_y, state.target_z};
            camera.distance = state.distance;
            camera.azimuth = state.azimuth;
            camera.elevation = state.elevation;
            camera.fov_y = state.fov_y;
            camera.near_clip = state.near_clip;
            camera.far_clip = state.far_clip;
            return camera;
        }

    } // namespace

    PlotScene3DChartChromeRenderer::PlotScene3DChartChromeRenderer()
        : canvas_(std::make_unique<tgfx::Canvas2DRenderer>()) {}

    PlotScene3DChartChromeRenderer::~PlotScene3DChartChromeRenderer() = default;

    void PlotScene3DChartChromeRenderer::draw_grid_labels(tgfx::RenderContext2& context,
                                                          tgfx::FontAtlas& font,
                                                          const PlotScene3DFrameRenderState& frame,
                                                          const tc_grid_item3d_style& style,
                                                          int viewport_width,
                                                          int viewport_height) {
        if (style.labels_visible == 0 || viewport_width <= 0 || viewport_height <= 0) {
            return;
        }

        const termin::OrbitCamera camera = make_camera(frame.camera);
        const float aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
        const termin::Mat44f mvp = camera.projection_matrix(aspect) * camera.view_matrix();

        const double dx = (frame.bounds_max[0] - frame.bounds_min[0]) * frame.axis_scale[0];
        const double dy = (frame.bounds_max[1] - frame.bounds_min[1]) * frame.axis_scale[1];
        const double dz = (frame.bounds_max[2] - frame.bounds_min[2]) * frame.axis_scale[2];
        const double data_size = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double offset = data_size * 0.03;
        constexpr float kTickTextSizePx = 14.0f;
        constexpr float kAxisLabelSizePx = 16.0f;
        const termin::SrgbColor label_color{0.8f, 0.8f, 0.8f, 1.0f};
        const auto project = [&](const termin::Vec3f& world) {
            const termin::Vec3 clip = mvp.transform_point({world.x, world.y, world.z});
            return tgfx::CanvasVec2{
                static_cast<float>((clip.x * 0.5 + 0.5) * viewport_width),
                static_cast<float>((1.0 - (clip.y * 0.5 + 0.5)) * viewport_height),
            };
        };

        context.set_depth_test(false);
        context.set_depth_write(false);
        context.set_blend(true);
        canvas_->set_default_font(&font);
        canvas_->begin(context, viewport_width, viewport_height);

        for (size_t axis = 0; axis < 3; ++axis) {
            for (double tick : axes::nice_ticks(frame.bounds_min[axis], frame.bounds_max[axis], 6)) {
                termin::Vec3f position{
                    static_cast<float>(frame.bounds_min[0] * frame.axis_scale[0]),
                    static_cast<float>(frame.bounds_min[1] * frame.axis_scale[1]),
                    static_cast<float>(frame.bounds_min[2] * frame.axis_scale[2]),
                };
                position[axis] = static_cast<float>(tick * frame.axis_scale[axis]);
                if (axis == 0) {
                    position[1] -= static_cast<float>(offset);
                } else {
                    position[0] -= static_cast<float>(offset);
                }
                const tgfx::CanvasVec2 screen = project(position);
                canvas_->draw_text(axes::format_tick(tick),
                                   screen.x,
                                   screen.y - kTickTextSizePx * 0.5f,
                                   kTickTextSizePx,
                                   label_color,
                                   &font,
                                   tgfx::Text2DRenderer::Anchor::Center);
            }
        }

        const std::string* labels[3] = {
            &frame.x_label,
            &frame.y_label,
            &frame.z_label,
        };
        for (size_t axis = 0; axis < 3; ++axis) {
            if (labels[axis]->empty()) {
                continue;
            }
            termin::Vec3f position{
                static_cast<float>(frame.bounds_min[0] * frame.axis_scale[0]),
                static_cast<float>(frame.bounds_min[1] * frame.axis_scale[1]),
                static_cast<float>(frame.bounds_min[2] * frame.axis_scale[2]),
            };
            position[axis] = static_cast<float>(frame.bounds_max[axis] * frame.axis_scale[axis]);
            if (axis == 0) {
                position[1] -= static_cast<float>(offset * 1.9);
            } else {
                position[0] -= static_cast<float>(offset * 1.9);
            }
            const tgfx::CanvasVec2 screen = project(position);
            canvas_->draw_text(*labels[axis],
                               screen.x,
                               screen.y - kAxisLabelSizePx * 0.5f,
                               kAxisLabelSizePx,
                               label_color,
                               &font,
                               tgfx::Text2DRenderer::Anchor::Center);
        }
        canvas_->end();
        context.set_depth_test(true);
        context.set_depth_write(true);
    }

    void PlotScene3DChartChromeRenderer::draw_colorbar(tgfx::RenderContext2& context,
                                                       tgfx::FontAtlas& font,
                                                       const PlotScene3DFrameRenderState& frame,
                                                       const tc_surface_item3d_style& surface_style,
                                                       const tc_colorbar3d_style& colorbar_style,
                                                       const std::string& label,
                                                       int viewport_width,
                                                       int viewport_height) {
        if (viewport_width <= 0 || viewport_height <= 0 ||
            surface_style.colormap == TC_PLOT_COLORMAP3D_SOLID) {
            return;
        }

        const float range_min = static_cast<float>(frame.bounds_min[2]);
        const float range_max = static_cast<float>(frame.bounds_max[2]);
        if (!std::isfinite(range_min) || !std::isfinite(range_max) || range_max <= range_min) {
            return;
        }

        const float available_height = std::max(1.0f, static_cast<float>(viewport_height) - 32.0f);
        const float bar_height = std::clamp(static_cast<float>(viewport_height) * colorbar_style.height_ratio,
                                            std::min(72.0f, available_height),
                                            available_height);
        const float bar_width = std::min(colorbar_style.width_px, std::max(1.0f, viewport_width * 0.2f));
        const float bar_y = (static_cast<float>(viewport_height) - bar_height) * 0.5f;
        const auto ticks = axes::nice_ticks(range_min, range_max, static_cast<int>(colorbar_style.tick_count));

        float widest_tick = 0.0f;
        for (double tick : ticks) {
            widest_tick = std::max(widest_tick,
                                   canvas_->measure_text(axes::format_tick(tick), colorbar_style.text_size_px, &font)
                                       .width);
        }
        const float right = static_cast<float>(viewport_width) - colorbar_style.margin_right_px - widest_tick;
        const float bar_x = std::max(4.0f, right - colorbar_style.text_gap_px - bar_width);

        context.set_depth_test(false);
        context.set_depth_write(false);
        context.set_blend(true);
        canvas_->set_default_font(&font);
        canvas_->begin(context, viewport_width, viewport_height);

        constexpr int kGradientSteps = 128;
        for (int step = 0; step < kGradientSteps; ++step) {
            const float t0 = static_cast<float>(step) / kGradientSteps;
            const float t1 = static_cast<float>(step + 1) / kGradientSteps;
            const float palette_t = surface_style.colormap_reversed != 0 ? 1.0f - (t0 + t1) * 0.5f
                                                                          : (t0 + t1) * 0.5f;
            const auto color = styles::colormap(static_cast<SurfaceColorMap>(surface_style.colormap), palette_t);
            const float y0 = bar_y + bar_height * (1.0f - t1);
            const float y1 = bar_y + bar_height * (1.0f - t0);
            canvas_->draw_rect(bar_x, y0, bar_width, std::max(1.0f, y1 - y0), color);
        }

        const termin::SrgbColor border{colorbar_style.border_r,
                                       colorbar_style.border_g,
                                       colorbar_style.border_b,
                                       colorbar_style.border_a};
        const termin::SrgbColor text_color{colorbar_style.label_r,
                                           colorbar_style.label_g,
                                           colorbar_style.label_b,
                                           colorbar_style.label_a};
        canvas_->draw_rect_outline(bar_x, bar_y, bar_width, bar_height, border, 1.0f);
        for (double tick : ticks) {
            const float normalized = std::clamp(static_cast<float>((tick - range_min) / (range_max - range_min)),
                                                0.0f,
                                                1.0f);
            const float y = bar_y + bar_height * (1.0f - normalized);
            canvas_->draw_line(bar_x + bar_width, y, bar_x + bar_width + 4.0f, y, border, 1.0f);
            canvas_->draw_text(axes::format_tick(tick),
                               bar_x + bar_width + colorbar_style.text_gap_px,
                               y - colorbar_style.text_size_px * 0.5f,
                               colorbar_style.text_size_px,
                               text_color,
                               &font,
                               tgfx::Text2DRenderer::Anchor::Left);
        }
        if (!label.empty()) {
            canvas_->draw_text(label,
                               bar_x + bar_width * 0.5f,
                               std::max(2.0f, bar_y - colorbar_style.text_size_px - 6.0f),
                               colorbar_style.text_size_px,
                               text_color,
                               &font,
                               tgfx::Text2DRenderer::Anchor::Center);
        }
        canvas_->end();
    }

    void PlotScene3DChartChromeRenderer::release_gpu() {
        canvas_->release_gpu();
    }

} // namespace tcplot
