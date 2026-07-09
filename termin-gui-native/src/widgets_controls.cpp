#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
Button::Button(std::string text)
    : NativeWidget("Button"), text_(std::move(text)) {
    set_style_role(TC_UI_STYLE_BUTTON);
    set_preferred_size(tc_ui_size {96.0f, 36.0f});
}

Button::Button(std::string text, Color fill)
    : Button(std::move(text)) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, fill.c_color());
}

Button::Button(Color fill)
    : Button(std::string {}, fill) {}

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
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {bounds().x + 14.0f, y},
        tc_ui_point {bounds().x + bounds().width - 14.0f, y},
        style.accent,
        style.border_width
    );
    if (!text_.empty()) {
        tc_ui_painter_push_clip(context, bounds());
        tc_ui_painter_draw_text(
            context,
            text_.c_str(),
            tc_ui_point {
                bounds().x + style.padding_left,
                centered_text_baseline(document, text_, style.font_size, bounds())
            },
            style.font_size,
            style.foreground
        );
        tc_ui_painter_pop_clip(context);
    }
}

tc_ui_event_result Button::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
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

Label::Label(std::string text)
    : NativeWidget("Label"), text_(std::move(text)) {
    set_style_role(TC_UI_STYLE_LABEL);
    update_unmeasured_size();
}

Label::Label(std::string text, float font_size)
    : Label(std::move(text)) {
    set_font_size(font_size);
}

Label::Label(std::string text, float font_size, Color color)
    : Label(std::move(text), font_size) {
    set_color(color);
}

Label& Label::set_text(std::string text) {
    text_ = std::move(text);
    update_unmeasured_size();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Label& Label::set_color(Color color) {
    set_style_color(*this, TC_UI_STYLE_FOREGROUND, color.c_color());
    return *this;
}

Label& Label::set_font_size(float font_size) {
    set_style_metric(*this, TC_UI_STYLE_FONT_SIZE, std::max(1.0f, font_size));
    update_unmeasured_size();
    return *this;
}

tc_ui_size Label::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    tc_ui_size measured = preferred_size();
    if (measure_text(document, text_, style.font_size, metrics)) {
        measured.width = metrics.width;
        measured.height = metrics.line_height > 0.0f ? metrics.line_height : metrics.height;
    }
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void Label::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    const bool has_metrics = measure_text(document, text_, style.font_size, metrics);
    const float line_height = has_metrics && metrics.line_height > 0.0f
        ? metrics.line_height
        : style.font_size;
    const float ascent = has_metrics && metrics.ascent > 0.0f ? metrics.ascent : style.font_size;
    const float baseline = bounds().y + std::max(0.0f, (bounds().height - line_height) * 0.5f) + ascent;
    tc_ui_painter_push_clip(context, bounds());
    tc_ui_painter_draw_text(
        context,
        text_.c_str(),
        tc_ui_point {bounds().x, baseline},
        style.font_size,
        style.foreground
    );
    tc_ui_painter_pop_clip(context);
}

void Label::update_unmeasured_size() {
    const tc_ui_style_override style_override = this->style_override();
    const float font_size = (style_override.fields & TC_UI_STYLE_FONT_SIZE) != 0
        ? style_override.value.font_size
        : 15.0f;
    set_preferred_size(tc_ui_size {0.0f, font_size});
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

Checkbox::Checkbox(bool checked)
    : NativeWidget("Checkbox"), checked_(checked) {
    set_style_role(TC_UI_STYLE_CHECKBOX);
    set_preferred_size(tc_ui_size {32.0f, 32.0f});
}

void Checkbox::set_checked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, checked_);
}

void Checkbox::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(
        document,
        checked_ ? TC_UI_STYLE_STATE_CHECKED : 0
    );
    const float side = std::min(bounds().width, bounds().height);
    const tc_ui_rect box {
        bounds().x + (bounds().width - side) * 0.5f,
        bounds().y + (bounds().height - side) * 0.5f,
        side,
        side
    };
    tc_ui_painter_fill_rect(context, box, style.background);
    tc_ui_painter_stroke_rect(context, box, style.border, std::max(2.0f, style.border_width));
    if (checked_) {
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {box.x + side * 0.22f, box.y + side * 0.55f},
            tc_ui_point {box.x + side * 0.43f, box.y + side * 0.74f},
            style.accent,
            3.0f
        );
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {box.x + side * 0.43f, box.y + side * 0.74f},
            tc_ui_point {box.x + side * 0.78f, box.y + side * 0.26f},
            style.accent,
            3.0f
        );
    }
}

tc_ui_event_result Checkbox::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
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
            set_checked(!checked_);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && captured) {
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

ProgressBar::ProgressBar(float value)
    : NativeWidget("ProgressBar") {
    set_style_role(TC_UI_STYLE_PROGRESS);
    set_preferred_size(tc_ui_size {120.0f, 20.0f});
    set_value(value);
}

void ProgressBar::set_value(float value) {
    const float next = clamp_float(value, 0.0f, 1.0f);
    if (std::fabs(next - value_) <= 0.0001f) {
        return;
    }
    value_ = next;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void ProgressBar::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_rect fill = bounds();
    fill.width *= value_;
    tc_ui_painter_fill_rect(context, fill, style.accent);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
}

Separator::Separator(Orientation orientation)
    : NativeWidget("Separator"), orientation_(orientation) {
    set_style_role(TC_UI_STYLE_SEPARATOR);
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {24.0f, 1.0f}
        : tc_ui_size {1.0f, 24.0f});
}

Separator& Separator::set_color(Color color) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, color.c_color());
    return *this;
}

Separator& Separator::set_thickness(float thickness) {
    const float next = std::max(1.0f, thickness);
    set_style_metric(*this, TC_UI_STYLE_BORDER_WIDTH, next);
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {preferred_size().width, next}
        : tc_ui_size {next, preferred_size().height});
    return *this;
}

void Separator::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const float thickness = std::max(1.0f, style.border_width);
    tc_ui_rect rect = bounds();
    if (orientation_ == Orientation::Horizontal) {
        rect.y += std::max(0.0f, (rect.height - thickness) * 0.5f);
        rect.height = std::min(rect.height, thickness);
    } else {
        rect.x += std::max(0.0f, (rect.width - thickness) * 0.5f);
        rect.width = std::min(rect.width, thickness);
    }
    tc_ui_painter_fill_rect(context, rect, style.background);
}


} // namespace termin::gui_native
