#include "widgets_internal.hpp"

#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

StatusBar::StatusBar(std::string text) : NativeWidget("StatusBar") {
    set_style_role(TC_UI_STYLE_PANEL);
    set_text(std::move(text));
    set_preferred_size(tc_ui_size{400.0f, 24.0f});
}

void StatusBar::set_text(std::string text) {
    if (!valid_utf8(text)) {
        tc_log_error("[termin-gui-native] StatusBar rejected invalid UTF-8 text");
        throw std::invalid_argument("status text must be valid UTF-8");
    }
    text_ = std::move(text);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void StatusBar::show_message(std::string message) {
    if (!valid_utf8(message)) {
        tc_log_error("[termin-gui-native] StatusBar rejected invalid UTF-8 message");
        throw std::invalid_argument("status message must be valid UTF-8");
    }
    message_ = std::move(message);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void StatusBar::clear_message() {
    if (message_.empty())
        return;
    message_.clear();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size StatusBar::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics{};
    measure_text(document, displayed_text(), std::max(9.0f, style.font_size - 2.0f), metrics);
    return clamp_size(
        tc_ui_size{std::max(preferred_size().width, metrics.width + padding_x_ * 2.0f),
                   std::max(preferred_size().height, metrics.height + padding_y_ * 2.0f)},
        constraints);
}

void StatusBar::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_draw_line(context, tc_ui_point{bounds().x, bounds().y},
                            tc_ui_point{bounds().x + bounds().width, bounds().y}, style.border,
                            style.border_width);
    if (!displayed_text().empty()) {
        tc_ui_color foreground = style.foreground;
        if (!has_message())
            foreground.a *= 0.65f;
        const float font_size = std::max(9.0f, style.font_size - 2.0f);
        tc_ui_painter_draw_text(
            context, displayed_text().c_str(),
            tc_ui_point{bounds().x + padding_x_, bounds().y + bounds().height * 0.68f}, font_size,
            foreground);
    }
}

} // namespace termin::gui_native
