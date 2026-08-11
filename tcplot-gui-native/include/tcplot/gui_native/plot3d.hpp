#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include <tcplot/retained_chart3d.h>
#include <termin/gui_native/render_prepared_widget.hpp>
#include <termin/gui_native/viewport3d.hpp>

#include "tcplot/gui_native/export.h"

namespace tcplot::gui_native {

    class TCPLOT_GUI_NATIVE_API Plot3D final : public termin::gui_native::Viewport3D,
                                               public termin::gui_native::RenderPreparedWidget {
    public:
        Plot3D();
        ~Plot3D() override;

        Plot3D(const Plot3D&) = delete;
        Plot3D& operator=(const Plot3D&) = delete;

        tc_plot_item3d_handle add_line(std::span<const double> x,
                                       std::span<const double> y,
                                       std::span<const double> z,
                                       const tc_line_item3d_style& style);
        tc_plot_item3d_handle add_scatter(std::span<const double> x,
                                          std::span<const double> y,
                                          std::span<const double> z,
                                          const tc_scatter_item3d_style& style);
        tc_plot_item3d_handle add_surface(std::span<const double> x,
                                          std::span<const double> y,
                                          std::span<const double> z,
                                          uint32_t rows,
                                          uint32_t columns,
                                          const tc_surface_item3d_style& style);
        bool destroy_item(tc_plot_item3d_handle item);
        void clear();

        void set_axis_labels(const char* x, const char* y, const char* z);
        bool set_axis_scale(float x, float y, float z);
        bool set_surface_shading(bool enabled, float strength = 0.35f);
        bool set_light_direction(float x, float y, float z);
        void fit_camera();
        void reset_camera();
        bool camera(tc_orbit_camera3d_state& state) const;
        bool set_camera(const tc_orbit_camera3d_state& state);

        uint64_t scene_id() const;
        size_t item_count() const;
        uint32_t texture_id() const;

        void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
        void prepare_render(tgfx::RenderContext2& context,
                            tgfx::FontAtlas& default_font,
                            float density_scale) override;
        void release_render_resources() override;
        void on_destroy(tc_ui_document_handle document) override;

    private:
        class Surface;
        std::shared_ptr<Surface> surface_;

        tc_retained_chart3d* chart() const;
        void invalidate();
        static void require_equal(std::span<const double> x,
                                  std::span<const double> y,
                                  std::span<const double> z);
    };

} // namespace tcplot::gui_native
