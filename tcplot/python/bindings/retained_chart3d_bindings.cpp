#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>

#include <memory>
#include <stdexcept>

#include <tgfx2/font_atlas.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/handles.hpp>

#include "conversion_helpers.hpp"
#include "tcplot/gpu_host.hpp"
#include "tcplot/retained_chart3d.h"
#include "tcplot/styles.hpp"

namespace nb = nanobind;

namespace tcplot_bindings {

    namespace {

        class PythonRetainedChart3D {
        public:
            PythonRetainedChart3D()
                : chart_(tc_retained_chart3d_create(nullptr)) {
                if (!chart_) {
                    throw std::runtime_error("failed to create detached RetainedChart3D");
                }
            }

            ~PythonRetainedChart3D() {
                if (chart_) {
                    tc_retained_chart3d_destroy(chart_);
                    chart_ = nullptr;
                }
                gpu_host_.reset();
            }

            PythonRetainedChart3D(const PythonRetainedChart3D&) = delete;
            PythonRetainedChart3D& operator=(const PythonRetainedChart3D&) = delete;

            tc_plot_item3d_handle add_line(nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                                           nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                                           nb::ndarray<double, nb::c_contig, nb::device::cpu> z,
                                           nb::object color,
                                           float thickness) {
                require_equal_xyz(x, y, z);
                const auto resolved = resolve_color(color, 0);
                const tc_line_item3d_style style{resolved.r, resolved.g, resolved.b, resolved.a, thickness};
                return require_handle(
                    tc_retained_chart3d_add_line(chart_, x.data(), y.data(), z.data(), x.size(), &style));
            }

            tc_plot_item3d_handle add_scatter(nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                                              nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                                              nb::ndarray<double, nb::c_contig, nb::device::cpu> z,
                                              nb::object color,
                                              float size) {
                require_equal_xyz(x, y, z);
                const auto resolved = resolve_color(color, 1);
                const tc_scatter_item3d_style style{resolved.r, resolved.g, resolved.b, resolved.a, size};
                return require_handle(
                    tc_retained_chart3d_add_scatter(chart_, x.data(), y.data(), z.data(), x.size(), &style));
            }

            tc_plot_item3d_handle add_surface(nb::ndarray<double, nb::c_contig, nb::device::cpu> x,
                                              nb::ndarray<double, nb::c_contig, nb::device::cpu> y,
                                              nb::ndarray<double, nb::c_contig, nb::device::cpu> z,
                                              std::uint32_t rows,
                                              std::uint32_t columns,
                                              nb::object color,
                                              tcplot::SurfaceColorMap colormap,
                                              bool wireframe) {
                require_equal_xyz(x, y, z);
                if (x.size() != static_cast<std::size_t>(rows) * columns) {
                    throw std::invalid_argument("surface dimensions do not match the flat arrays");
                }
                const auto resolved = resolve_color(color, 2);
                tc_surface_item3d_style style{};
                style.color_r = resolved.r;
                style.color_g = resolved.g;
                style.color_b = resolved.b;
                style.color_a = resolved.a;
                style.colormap = static_cast<std::uint32_t>(colormap);
                style.wireframe = wireframe ? 1u : 0u;
                style.surface_grid_row_step = 8;
                style.surface_grid_col_step = 8;
                style.surface_grid_width_px = 1.5f;
                style.surface_grid_r = 0.05f;
                style.surface_grid_g = 0.05f;
                style.surface_grid_b = 0.05f;
                style.surface_grid_a = 1.0f;
                return require_handle(
                    tc_retained_chart3d_add_surface(chart_, x.data(), y.data(), z.data(), rows, columns, &style));
            }

            bool set_surface_grid(tc_plot_item3d_handle item,
                                  bool visible,
                                  std::uint32_t row_step,
                                  std::uint32_t column_step,
                                  nb::object color,
                                  float width_px) {
                tc_surface_item3d_style style{};
                if (!tc_retained_chart3d_surface_get_style(chart_, item, &style)) {
                    return false;
                }
                const auto resolved = resolve_color(color, 0);
                style.surface_grid_visible = visible ? 1u : 0u;
                style.surface_grid_row_step = row_step;
                style.surface_grid_col_step = column_step;
                style.surface_grid_r = resolved.r;
                style.surface_grid_g = resolved.g;
                style.surface_grid_b = resolved.b;
                style.surface_grid_a = resolved.a;
                style.surface_grid_width_px = width_px;
                return tc_retained_chart3d_surface_set_style(chart_, item, &style) != 0;
            }

            bool set_surface_wireframe(tc_plot_item3d_handle item, bool enabled) {
                tc_surface_item3d_style style{};
                if (!tc_retained_chart3d_surface_get_style(chart_, item, &style)) {
                    return false;
                }
                style.wireframe = enabled ? 1u : 0u;
                return tc_retained_chart3d_surface_set_style(chart_, item, &style) != 0;
            }

            bool destroy_item(tc_plot_item3d_handle item) {
                return tc_retained_chart3d_destroy_item(chart_, item) != 0;
            }

            void set_axis_labels(const std::string& x, const std::string& y, const std::string& z) {
                tc_retained_chart3d_set_axis_labels(chart_, x.c_str(), y.c_str(), z.c_str());
            }

            void set_axis_scale(float x, float y, float z) {
                require_success(tc_retained_chart3d_set_axis_scale(chart_, x, y, z), "set_axis_scale");
            }

            void set_surface_shading(bool enabled, float strength) {
                require_success(tc_retained_chart3d_set_surface_shading(chart_, enabled ? 1 : 0, strength),
                                "set_surface_shading");
            }

            void set_light_direction(float x, float y, float z) {
                require_success(tc_retained_chart3d_set_light_direction(chart_, x, y, z), "set_light_direction");
            }

            bool pointer_down(float x, float y, int button) {
                return tc_retained_chart3d_pointer_down(chart_, x, y, button) != 0;
            }

            void pointer_move(float x, float y) {
                tc_retained_chart3d_pointer_move(chart_, x, y);
            }

            void pointer_up(float x, float y, int button) {
                tc_retained_chart3d_pointer_up(chart_, x, y, button);
            }

            bool wheel(float x, float y, float delta) {
                return tc_retained_chart3d_wheel(chart_, x, y, delta) != 0;
            }

            tgfx::TextureHandle render(tgfx::GraphicsHost& graphics, tgfx::FontAtlas& font, int width, int height) {
                attach(graphics, font);
                tgfx::TextureHandle result{};
                result.id = tc_retained_chart3d_render(chart_, width, height);
                if (!result) {
                    throw std::runtime_error("RetainedChart3D render failed");
                }
                return result;
            }

            void release_gpu() {
                tc_retained_chart3d_release_gpu(chart_);
            }

        private:
            template <typename Array> static void require_equal_xyz(const Array& x, const Array& y, const Array& z) {
                if (x.size() != y.size() || x.size() != z.size()) {
                    throw std::invalid_argument("x, y and z arrays must have equal size");
                }
            }

            static tcplot::Color4 resolve_color(nb::object color, std::uint32_t index) {
                if (!color.is_none())
                    return color_from_seq(color);
                return tcplot::styles::cycle_color(index);
            }

            static tc_plot_item3d_handle require_handle(tc_plot_item3d_handle item) {
                if (item.scene_id == 0) {
                    throw std::runtime_error("RetainedChart3D item creation failed");
                }
                return item;
            }

            static void require_success(int result, const char* operation) {
                if (!result) {
                    throw std::runtime_error(std::string("RetainedChart3D ") + operation + " failed");
                }
            }

            void attach(tgfx::GraphicsHost& graphics, tgfx::FontAtlas& font) {
                if (graphics_ == &graphics && font_ == &font)
                    return;
                auto next_host = std::make_unique<tcplot::GpuHost>(graphics, font);
                require_success(tc_retained_chart3d_attach_gpu_host(chart_, next_host.get()), "attach_gpu_host");
                gpu_host_ = std::move(next_host);
                graphics_ = &graphics;
                font_ = &font;
            }

            std::unique_ptr<tcplot::GpuHost> gpu_host_;
            tc_retained_chart3d* chart_ = nullptr;
            tgfx::GraphicsHost* graphics_ = nullptr;
            tgfx::FontAtlas* font_ = nullptr;
        };

    } // namespace

    void bind_retained_chart3d(nb::module_& m) {
        nb::class_<tc_plot_item3d_handle>(m, "PlotItem3DHandle")
            .def_prop_ro("scene_id", [](const tc_plot_item3d_handle& value) { return value.scene_id; })
            .def_prop_ro("index", [](const tc_plot_item3d_handle& value) { return value.index; })
            .def_prop_ro("generation", [](const tc_plot_item3d_handle& value) { return value.generation; });

        nb::class_<PythonRetainedChart3D>(m, "RetainedChart3D")
            .def(nb::init<>())
            .def("add_line",
                 &PythonRetainedChart3D::add_line,
                 nb::arg("x"),
                 nb::arg("y"),
                 nb::arg("z"),
                 nb::arg("color") = nb::none(),
                 nb::arg("thickness") = 1.5f)
            .def("add_scatter",
                 &PythonRetainedChart3D::add_scatter,
                 nb::arg("x"),
                 nb::arg("y"),
                 nb::arg("z"),
                 nb::arg("color") = nb::none(),
                 nb::arg("size") = 4.0f)
            .def("add_surface",
                 &PythonRetainedChart3D::add_surface,
                 nb::arg("x"),
                 nb::arg("y"),
                 nb::arg("z"),
                 nb::arg("rows"),
                 nb::arg("columns"),
                 nb::arg("color") = nb::none(),
                 nb::arg("colormap") = tcplot::SurfaceColorMap::Jet,
                 nb::arg("wireframe") = false)
            .def("set_surface_grid",
                 &PythonRetainedChart3D::set_surface_grid,
                 nb::arg("item"),
                 nb::arg("visible"),
                 nb::arg("row_step"),
                 nb::arg("column_step"),
                 nb::arg("color"),
                 nb::arg("width_px") = 1.5f)
            .def("set_surface_wireframe", &PythonRetainedChart3D::set_surface_wireframe)
            .def("destroy_item", &PythonRetainedChart3D::destroy_item)
            .def("set_axis_labels", &PythonRetainedChart3D::set_axis_labels)
            .def("set_axis_scale", &PythonRetainedChart3D::set_axis_scale)
            .def("set_surface_shading",
                 &PythonRetainedChart3D::set_surface_shading,
                 nb::arg("enabled"),
                 nb::arg("strength") = 0.35f)
            .def("set_light_direction", &PythonRetainedChart3D::set_light_direction)
            .def("pointer_down", &PythonRetainedChart3D::pointer_down)
            .def("pointer_move", &PythonRetainedChart3D::pointer_move)
            .def("pointer_up", &PythonRetainedChart3D::pointer_up)
            .def("wheel", &PythonRetainedChart3D::wheel)
            .def("render",
                 &PythonRetainedChart3D::render,
                 nb::arg("graphics_host"),
                 nb::arg("font"),
                 nb::arg("width"),
                 nb::arg("height"))
            .def("release_gpu", &PythonRetainedChart3D::release_gpu);
    }

} // namespace tcplot_bindings
