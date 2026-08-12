#include <tcplot/gui_native/plot2d.hpp>
#include <tcplot/gui_native/widget_registration.hpp>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/tc_widget_registry.h>
#include <termin/gui_native/uiscript.hpp>

namespace {

    [[noreturn]] void fail(std::string_view message) {
        std::cerr << "tcplot_gui_native_plot2d_test: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void require(bool condition, std::string_view message) {
        if (!condition) {
            fail(message);
        }
    }

    bool measure_text(void*, const char* text, std::size_t length, float font_size, tc_ui_text_metrics* out) {
        if (text == nullptr || out == nullptr || font_size <= 0.0f) {
            return false;
        }
        out->width = static_cast<float>(length) * font_size * 0.55f;
        out->height = font_size;
        out->ascent = font_size * 0.8f;
        out->descent = font_size * 0.2f;
        out->line_height = font_size * 1.2f;
        return true;
    }

} // namespace

int main() {
    using tcplot::gui_native::Plot2D;
    using termin::gui_native::TcDocument;

    require(tcplot::gui_native::register_plot_widget_types(), "Plot2D registration failed");
    require(tc_widget_registry_has("termin.gui.Plot2D"), "Plot2D is absent from the widget registry");

    const TcDocument loaded_document{tc_ui_document_create()};
    require(loaded_document.valid(), "UiScript document creation failed");
    require(loaded_document.set_presentation_metrics(tc_ui_presentation_metrics_identity({640.0f, 320.0f})),
            "UiScript presentation metrics were rejected");
    loaded_document.set_text_measurer(&measure_text, nullptr);
    {
        termin::gui_native::UiScriptLoader loader;
        auto loaded = loader.load_string("uiscript: 2\n"
                                         "root:\n"
                                         "  type: termin.gui.Plot2D\n"
                                         "  name: chart\n"
                                         "  title: Servo coordinate\n"
                                         "  x_label: time, s\n"
                                         "  y_label: angle, deg\n"
                                         "  auto_fit: true\n",
                                         loaded_document);
        tc_widget* loaded_widget =
            tc_ui_document_resolve_widget(loaded.document().handle(), loaded.named("chart").handle);
        require(loaded_widget != nullptr, "UiScript did not create Plot2D");
        auto* loaded_plot = dynamic_cast<Plot2D*>(static_cast<termin::gui_native::Widget*>(loaded_widget->body));
        require(loaded_plot != nullptr, "UiScript widget has the wrong C++ type");
        require(loaded_plot->title() == "Servo coordinate", "UiScript title was not applied");

        bool invalid_property_rejected = false;
        try {
            (void)loader.parser.parse("uiscript: 2\n"
                                      "root:\n"
                                      "  type: termin.gui.Plot2D\n"
                                      "  auto_fit: definitely\n");
        } catch (const termin::gui_native::UiScriptError&) {
            invalid_property_rejected = true;
        }
        require(invalid_property_rejected, "UiScript accepted an invalid Plot2D property type");
    }
    tc_ui_document_destroy(loaded_document.handle());

    const TcDocument document{tc_ui_document_create()};
    require(document.valid(), "document creation failed");
    require(document.set_presentation_metrics(tc_ui_presentation_metrics_identity({640.0f, 320.0f})),
            "presentation metrics were rejected");
    document.set_text_measurer(&measure_text, nullptr);

    auto* plot = new Plot2D();
    const tc_widget_handle handle = document.adopt(plot);
    require(!tc_widget_handle_is_invalid(handle), "widget adoption failed");
    require(document.add_root(*plot), "adding the root widget failed");

    plot->set_title("Servo coordinate");
    plot->set_x_label("time, s");
    plot->set_y_label("angle, deg");
    const std::size_t actual = plot->add_line();
    const std::size_t target = plot->add_line();
    require(actual == 0, "unexpected first line index");
    require(target == 1, "unexpected second line index");

    const double time[] = {0.0, 0.5, 1.0, 1.5};
    const double coordinate[] = {15.0, 42.0, 76.0, 89.0};
    const double target_coordinate[] = {90.0, 90.0, 90.0, 90.0};
    require(plot->set_line_data(actual, time, coordinate), "setting actual line data failed");
    require(plot->set_line_data(target, time, target_coordinate), "setting target line data failed");
    require(plot->line_count() == 2, "unexpected line count");

    tcplot::PlotScatterSeriesStyle2D scatter_style;
    scatter_style.diameter_px = 7.0f;
    const std::size_t samples = plot->add_scatter(scatter_style);
    require(samples == 0, "unexpected first scatter index");
    require(plot->set_scatter_data(samples, time, coordinate), "setting scatter data failed");
    require(plot->scatter_count() == 1, "unexpected scatter count");

    const auto marker = plot->create_data_marker(0.5, 42.0, "drag me", actual);
    require(marker.valid(), "creating retained data marker failed");

    document.layout_roots({0.0f, 0.0f, 640.0f, 320.0f});
    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    require(draw_list != nullptr, "draw-list creation failed");
    require(paint_context != nullptr, "paint-context creation failed");
    document.paint_roots(paint_context);

    bool has_retained_chart = false;
    for (std::size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
        if (command != nullptr && command->type == TC_UI_DRAW_CANVAS2D_LIST) {
            has_retained_chart = true;
            break;
        }
    }
    require(has_retained_chart, "painting did not emit the retained chart");

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
    tc_ui_document_destroy(document.handle());
    return 0;
}
