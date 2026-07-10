#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

GroupBox::GroupBox(std::string title, const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "GroupBox"),
      title_(std::move(title)) {
    set_style_role(TC_UI_STYLE_GROUP_BOX);
    set_preferred_size(tc_ui_size {240.0f, 140.0f});
}

GroupBox& GroupBox::set_title(std::string title) {
    title_ = std::move(title);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GroupBox& GroupBox::set_padding(EdgeInsets padding) {
    tc_ui_style_override style_override = this->style_override();
    style_override.fields |= TC_UI_STYLE_PADDING_LEFT | TC_UI_STYLE_PADDING_TOP |
        TC_UI_STYLE_PADDING_RIGHT | TC_UI_STYLE_PADDING_BOTTOM;
    style_override.value.padding_left = std::max(0.0f, padding.left);
    style_override.value.padding_top = std::max(0.0f, padding.top);
    style_override.value.padding_right = std::max(0.0f, padding.right);
    style_override.value.padding_bottom = std::max(0.0f, padding.bottom);
    if (!set_style_override(style_override)) {
        throw std::runtime_error("failed to set GroupBox padding style override");
    }
    return *this;
}

GroupBox& GroupBox::set_background(Color color) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, color.c_color());
    return *this;
}

GroupBox& GroupBox::set_border(Color color, float thickness) {
    set_style_color(*this, TC_UI_STYLE_BORDER, color.c_color());
    set_style_metric(*this, TC_UI_STYLE_BORDER_WIDTH, std::max(0.0f, thickness));
    return *this;
}

void GroupBox::set_content(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid GroupBox content handle");
        return;
    }
    const tc_widget_handle previous = this->content();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (!attach_child(c_widget(), handle, 0, "GroupBox::set_content")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size GroupBox::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    tc_ui_size measured = preferred_size();
    tc_ui_text_metrics title_metrics {};
    if (measure_text(document, title_, style.font_size, title_metrics)) {
        measured.width = std::max(
            measured.width,
            title_metrics.width + style.padding_left + style.padding_right
        );
    }
    if (!tc_widget_handle_is_invalid(this->content())) {
        if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::measure")) {
            const tc_ui_size content_size = measure_widget(content, document, unconstrained());
            measured.width = std::max(
                measured.width,
                content_size.width + style.padding_left + style.padding_right
            );
            measured.height = std::max(
                measured.height,
                content_size.height + header_height_ + style.padding_top + style.padding_bottom
            );
        }
    }
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void GroupBox::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (tc_widget_handle_is_invalid(this->content())) {
        return;
    }
    tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::layout");
    layout_widget(content, document, content_rect(document));
}

void GroupBox::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    if (color_visible(style.background)) {
        tc_ui_painter_fill_rect(context, bounds(), style.background);
    }
    if (color_visible(style.border) && style.border_width > 0.0f) {
        tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {bounds().x, bounds().y + header_height_},
            tc_ui_point {bounds().x + bounds().width, bounds().y + header_height_},
            style.border,
            style.border_width
        );
    }
    if (!title_.empty()) {
        tc_ui_rect title_clip {
            bounds().x + style.padding_left,
            bounds().y,
            std::max(0.0f, bounds().width - style.padding_left - style.padding_right),
            header_height_
        };
        tc_ui_painter_push_clip(context, title_clip);
        tc_ui_painter_draw_text(
            context,
            title_.c_str(),
            tc_ui_point {
                bounds().x + style.padding_left,
                centered_text_baseline(document, title_, style.font_size, title_clip)
            },
            style.font_size,
            style.foreground
        );
        tc_ui_painter_pop_clip(context);
    }

    const tc_ui_rect clip = content_rect(document);
    tc_ui_painter_push_clip(context, clip);
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::paint");
        paint_widget(content, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result GroupBox::pointer_event(tc_ui_document*, const tc_ui_pointer_event*) {
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle GroupBox::hit_test(tc_ui_document* document, float x, float y) {
    if (!visible() || !rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(this->content()) && rect_contains(content_rect(document), x, y)) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::hit_test");
        if (content && content->vtable && content->vtable->hit_test) {
            tc_widget_handle hit = content->vtable->hit_test(content, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

tc_ui_rect GroupBox::content_rect(tc_ui_document* document) const {
    const tc_ui_style style = computed_style(document);
    tc_ui_rect rect {
        bounds().x + style.padding_left,
        bounds().y + header_height_ + style.padding_top,
        std::max(0.0f, bounds().width - style.padding_left - style.padding_right),
        std::max(0.0f, bounds().height - header_height_ - style.padding_top - style.padding_bottom)
    };
    return rect;
}


} // namespace termin::gui_native
