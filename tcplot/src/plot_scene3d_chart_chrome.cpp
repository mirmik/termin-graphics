#include "plot_scene3d_chart_chrome.hpp"

#include <cmath>

#include <termin/camera/orbit_camera.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/text3d_renderer.hpp>

#include "tcplot/axes.hpp"

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
        : text_(std::make_unique<tgfx::Text3DRenderer>()) {}

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
        const termin::Mat44f view = camera.view_matrix();
        const termin::Vec3f camera_right{
            view.data[0 * 4 + 0],
            view.data[1 * 4 + 0],
            view.data[2 * 4 + 0],
        };
        const termin::Vec3f camera_up{
            view.data[0 * 4 + 1],
            view.data[1 * 4 + 1],
            view.data[2 * 4 + 1],
        };

        const double dx = (frame.bounds_max[0] - frame.bounds_min[0]) * frame.axis_scale[0];
        const double dy = (frame.bounds_max[1] - frame.bounds_min[1]) * frame.axis_scale[1];
        const double dz = (frame.bounds_max[2] - frame.bounds_min[2]) * frame.axis_scale[2];
        const double data_size = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double offset = data_size * 0.03;
        constexpr float kTickTextSizePx = 14.0f;
        constexpr float kAxisLabelSizePx = 16.0f;
        const termin::Color4 label_color{0.8f, 0.8f, 0.8f, 1.0f};

        text_->set_expansion_mode(tgfx::Text3DRenderer::ExpansionMode::ScreenAligned);
        context.set_depth_test(false);
        context.set_blend(true);
        text_->begin(&context, mvp.data, camera_right, camera_up, &font);

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
                text_->draw(axes::format_tick(tick),
                            tgfx::Text3DRenderer::DrawOptions{
                                position,
                                label_color,
                                kTickTextSizePx,
                                tgfx::Text3DRenderer::Anchor::Center,
                            });
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
            text_->draw(*labels[axis],
                        tgfx::Text3DRenderer::DrawOptions{
                            position,
                            label_color,
                            kAxisLabelSizePx,
                            tgfx::Text3DRenderer::Anchor::Center,
                        });
        }
        text_->end();
        context.set_depth_test(true);
    }

    void PlotScene3DChartChromeRenderer::release_gpu() {
        text_->release_gpu();
    }

} // namespace tcplot
