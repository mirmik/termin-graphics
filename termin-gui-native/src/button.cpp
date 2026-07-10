#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

Button::Button(std::string text) : NativeWidget("Button"), text_(std::move(text)) {
    set_style_role(TC_UI_STYLE_BUTTON);
    set_focusable(true);
    set_preferred_size(tc_ui_size{96.0f, 36.0f});
}

tc_ui_event_result Button::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN ||
        (event->key != TC_UI_KEY_ENTER && event->key != ' ')) {
        return TC_UI_EVENT_IGNORED;
    }
    clicked_.emit(*this);
    return TC_UI_EVENT_HANDLED;
}

Button::Button(std::string text, Color fill) : Button(std::move(text)) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, fill.c_color());
}

Button::Button(Color fill) : Button(std::string{}, fill) {}

Button& Button::set_accent(Color color) {
    set_style_color(*this, TC_UI_STYLE_ACCENT, color.c_color());
    set_style_color(*this, TC_UI_STYLE_BORDER, color.c_color());
    return *this;
}

Button& Button::set_text(std::string text) {
    text_ = std::move(text);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

void Button::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
    const float y = bounds().y + bounds().height * 0.5f;
    tc_ui_painter_draw_line(context, tc_ui_point{bounds().x + 14.0f, y},
                            tc_ui_point{bounds().x + bounds().width - 14.0f, y}, style.accent,
                            style.border_width);
    if (!text_.empty()) {
        tc_ui_painter_push_clip(context, bounds());
        tc_ui_painter_draw_text(
            context, text_.c_str(),
            tc_ui_point{bounds().x + style.padding_left,
                        centered_text_baseline(document, text_, style.font_size, bounds())},
            style.font_size, style.foreground);
        tc_ui_painter_pop_clip(context);
    }
}

tc_ui_event_result Button::pointer_event(tc_ui_document* document,
                                         const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        pressed_ = true;
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && (pressed_ || captured)) {
        const bool activate = pressed_ && rect_contains(bounds(), event->x, event->y);
        pressed_ = false;
        if (captured) {
            tc_ui_document_release_pointer_capture(document, handle());
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        if (activate) {
            clicked_.emit(*this);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && captured) {
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

} // namespace termin::gui_native
