#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include <tcplot/plot_series_item2d.hpp>
#include <tcplot/plot_annotations2d.hpp>
#include <termin/gui_native/native_widget.hpp>

#include "tcplot/gui_native/export.h"

namespace tcplot::gui_native {

    class TCPLOT_GUI_NATIVE_API Plot2D final : public termin::gui_native::NativeWidget {
    public:
        Plot2D();
        ~Plot2D() override;

        Plot2D(const Plot2D&) = delete;
        Plot2D& operator=(const Plot2D&) = delete;

        [[nodiscard]] const std::string& title() const noexcept;
        void set_title(std::string title);
        [[nodiscard]] const std::string& x_label() const noexcept;
        void set_x_label(std::string label);
        [[nodiscard]] const std::string& y_label() const noexcept;
        void set_y_label(std::string label);

        [[nodiscard]] bool auto_fit() const noexcept;
        void set_auto_fit(bool enabled);
        void set_view(double x_min, double x_max, double y_min, double y_max);

        [[nodiscard]] std::size_t line_count() const noexcept;
        std::size_t add_line();
        std::size_t add_line(PlotLineSeriesStyle2D style);
        bool set_line_data(std::size_t index, std::span<const double> x, std::span<const double> y);
        bool append_line_data(std::size_t index, std::span<const double> x, std::span<const double> y);
        void clear_lines();

        [[nodiscard]] std::size_t scatter_count() const noexcept;
        std::size_t add_scatter(PlotScatterSeriesStyle2D style);
        bool set_scatter_data(std::size_t index, std::span<const double> x, std::span<const double> y);
        void clear_scatters();

        PlotAnnotationHandle create_data_marker(double x,
                                                double y,
                                                std::string text,
                                                std::size_t snap_line_index);

        tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
        void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
        void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
        tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        void update_chart_layout();
        void update_auto_range();
        void invalidate_chart();
    };

} // namespace tcplot::gui_native
