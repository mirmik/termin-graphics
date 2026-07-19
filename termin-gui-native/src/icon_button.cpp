#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

IconButton::IconButton(std::string icon)
    : NativeWidget("IconButton"), icon_(std::move(icon)) {
    set_style_role(TC_UI_STYLE_BUTTON);
    set_cursor_intent(TC_UI_CURSOR_HAND);
    set_preferred_size(tc_ui_size {28.0f, 28.0f});
}

void IconButton::set_icon(std::string icon) {
    if (!valid_utf8(icon)) {
        tc_log_error("[termin-gui-native] IconButton rejected invalid UTF-8 icon");
        return;
    }
    icon_ = std::move(icon);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void IconButton::set_texture(uint32_t texture_id) {
    texture_id_ = texture_id;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void IconButton::set_active(bool active) {
    active_ = active;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void IconButton::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const uint32_t extra = (active_ ? TC_UI_STYLE_STATE_CHECKED : 0) |
        (pressed_ ? TC_UI_STYLE_STATE_PRESSED : 0);
    const tc_ui_style style = computed_style(document, extra);
    tc_ui_painter_fill_rounded_rect(context, bounds(), style.corner_radius, style.background);
    if (texture_id_ != 0) {
        const float inset = 5.0f;
        tc_ui_painter_draw_texture(context, texture_id_, tc_ui_rect {bounds().x + inset, bounds().y + inset, bounds().width - inset * 2.0f, bounds().height - inset * 2.0f}, style.foreground, false);
    } else if (!icon_.empty()) {
        tc_ui_text_metrics metrics {};
        measure_text(document, icon_, style.font_size, metrics);
        tc_ui_painter_draw_text(context, icon_.c_str(), tc_ui_point {bounds().x + (bounds().width - metrics.width) * 0.5f, bounds().y + bounds().height * 0.68f}, style.font_size, style.foreground);
    }
}

tc_ui_event_result IconButton::pointer_event(tc_ui_document*, const tc_ui_pointer_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        pressed_ = true;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && pressed_) {
        pressed_ = false;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        if (rect_contains(bounds(), event->x, event->y)) clicked_.emit(*this);
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}


} // namespace termin::gui_native
