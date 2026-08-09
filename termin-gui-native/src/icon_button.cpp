#include "widgets_internal.hpp"

#include <algorithm>

namespace termin::gui_native {
    using namespace detail;

    IconButton::IconButton(std::string icon)
        : NativeWidget("IconButton"),
          icon_(std::move(icon)) {
        set_style_role(TC_UI_STYLE_BUTTON);
        set_cursor_intent(TC_UI_CURSOR_HAND);
        set_focusable(true);
        set_preferred_size(tc_ui_size{28.0f, 28.0f});
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

    void IconButton::set_tooltip(std::string tooltip) {
        if (!valid_utf8(tooltip)) {
            tc_log_error("[termin-gui-native] IconButton rejected invalid UTF-8 tooltip");
            return;
        }
        tooltip_ = std::move(tooltip);
    }

    void IconButton::set_background_color(SrgbColor color) {
        background_color_ = color;
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void IconButton::set_hover_color(SrgbColor color) {
        hover_color_ = color;
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void IconButton::set_pressed_color(SrgbColor color) {
        pressed_color_ = color;
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void IconButton::set_active_color(SrgbColor color) {
        active_color_ = color;
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void IconButton::set_icon_color(SrgbColor color) {
        icon_color_ = color;
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void IconButton::set_corner_radius(float radius) {
        corner_radius_ = std::max(0.0f, radius);
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void IconButton::set_font_size(float size) {
        font_size_ = std::max(1.0f, size);
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }

    void IconButton::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
        const uint32_t extra = (active_ ? TC_UI_STYLE_STATE_CHECKED : 0) |
                               ((pressed_ || keyboard_pressed_) ? TC_UI_STYLE_STATE_PRESSED : 0);
        tc_ui_style style = computed_style(document, extra);
        const uint32_t state = tc_ui_document_widget_style_state(document, c_widget()) | extra;
        if (background_color_)
            style.background = to_tc_ui_srgb(*background_color_);
        if ((state & TC_UI_STYLE_STATE_HOVERED) != 0 && hover_color_)
            style.background = to_tc_ui_srgb(*hover_color_);
        if ((state & TC_UI_STYLE_STATE_PRESSED) != 0 && pressed_color_)
            style.background = to_tc_ui_srgb(*pressed_color_);
        if ((state & TC_UI_STYLE_STATE_CHECKED) != 0 && active_color_)
            style.background = to_tc_ui_srgb(*active_color_);
        if (icon_color_)
            style.foreground = to_tc_ui_srgb(*icon_color_);
        if (corner_radius_)
            style.corner_radius = *corner_radius_;
        if (font_size_)
            style.font_size = *font_size_;
        tc_ui_painter_fill_rounded_rect(context, bounds(), style.corner_radius, style.background);
        if (texture_id_ != 0) {
            const float inset = 5.0f;
            tc_ui_painter_draw_texture(context,
                                       texture_id_,
                                       tc_ui_rect{bounds().x + inset,
                                                  bounds().y + inset,
                                                  bounds().width - inset * 2.0f,
                                                  bounds().height - inset * 2.0f},
                                       style.foreground,
                                       TC_UI_TEXTURE_SAMPLING_LINEAR,
                                       false);
        } else if (!icon_.empty()) {
            tc_ui_text_metrics metrics{};
            measure_text(document, icon_, style.font_size, metrics);
            tc_ui_painter_draw_text(
                context,
                icon_.c_str(),
                tc_ui_point{bounds().x + (bounds().width - metrics.width) * 0.5f, bounds().y + bounds().height * 0.68f},
                style.font_size,
                style.foreground);
        }
    }

    tc_ui_event_result IconButton::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
        if (!event)
            return TC_UI_EVENT_IGNORED;
        if (event->type == TC_UI_POINTER_CANCEL) {
            const bool was_pressed = pressed_;
            pressed_ = false;
            if (was_pressed)
                mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            return was_pressed ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
        }
        const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
        if (event->type == TC_UI_POINTER_DOWN &&
            event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT) &&
            rect_contains(bounds(), event->x, event->y)) {
            pressed_ = true;
            tc_ui_document_set_pointer_capture(document, handle());
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_UP && (pressed_ || captured)) {
            const bool activate = pressed_ && rect_contains(bounds(), event->x, event->y);
            pressed_ = false;
            if (captured)
                tc_ui_document_release_pointer_capture(document, handle());
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            if (activate)
                clicked_.emit(*this);
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_MOVE && captured)
            return TC_UI_EVENT_HANDLED;
        return TC_UI_EVENT_IGNORED;
    }

    tc_ui_event_result IconButton::key_event(tc_ui_document_handle, const tc_ui_key_event* event) {
        if (!event)
            return TC_UI_EVENT_IGNORED;
        if (event->key == TC_UI_KEY_ENTER && event->type == TC_UI_KEY_DOWN) {
            if (!event->repeat)
                clicked_.emit(*this);
            return TC_UI_EVENT_HANDLED;
        }
        if (event->key != TC_UI_KEY_SPACE)
            return TC_UI_EVENT_IGNORED;
        if (event->type == TC_UI_KEY_DOWN) {
            if (!event->repeat && !keyboard_pressed_) {
                keyboard_pressed_ = true;
                mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            }
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_KEY_UP) {
            const bool activate = keyboard_pressed_;
            keyboard_pressed_ = false;
            if (activate) {
                mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
                clicked_.emit(*this);
            }
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }

    void IconButton::focus_event(tc_ui_document_handle, bool focused) {
        if (!focused && keyboard_pressed_) {
            keyboard_pressed_ = false;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
    }

} // namespace termin::gui_native
