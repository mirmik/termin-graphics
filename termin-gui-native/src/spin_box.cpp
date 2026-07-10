#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

SpinBox::SpinBox(float value)
    : NativeWidget("SpinBox") {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_focusable(true);
    set_preferred_size(tc_ui_size {120.0f, 34.0f});
    set_value(value);
}

void SpinBox::set_value(float value) {
    if (!std::isfinite(value)) {
        tc_log_error("[termin-gui-native] SpinBox rejected non-finite value");
        return;
    }
    const float next = clamp_float(value, min_value_, max_value_);
    if (std::fabs(next - value_) <= 0.000001f) return;
    value_ = next;
    if (editing_) {
        edit_text_ = formatted_value();
        caret_ = edit_text_.size();
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, value_);
}

void SpinBox::set_range(float min_value, float max_value) {
    if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value < min_value) {
        tc_log_error("[termin-gui-native] SpinBox rejected invalid range");
        return;
    }
    min_value_ = min_value;
    max_value_ = max_value;
    set_value(value_);
}

void SpinBox::set_step(float step) {
    if (!std::isfinite(step) || step <= 0.0f) {
        tc_log_error("[termin-gui-native] SpinBox rejected invalid step");
        return;
    }
    step_ = step;
}

void SpinBox::set_decimals(int decimals) {
    if (decimals < 0 || decimals > 9) {
        tc_log_error("[termin-gui-native] SpinBox rejected decimals outside [0, 9]");
        return;
    }
    decimals_ = decimals;
    if (editing_) {
        edit_text_ = formatted_value();
        caret_ = edit_text_.size();
    }
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

std::string SpinBox::formatted_value() const {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals_) << value_;
    return stream.str();
}

void SpinBox::begin_edit() {
    if (editing_) return;
    editing_ = true;
    edit_text_ = formatted_value();
    caret_ = edit_text_.size();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void SpinBox::commit_edit() {
    if (!editing_) return;
    editing_ = false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(edit_text_.c_str(), &end);
    if (errno == ERANGE || end == edit_text_.c_str() || !end || *end != '\0' || !std::isfinite(parsed)) {
        tc_log_error("[termin-gui-native] SpinBox rejected invalid numeric edit '%s'", edit_text_.c_str());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return;
    }
    set_value(parsed);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void SpinBox::cancel_edit() {
    editing_ = false;
    edit_text_.clear();
    caret_ = 0;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_rect SpinBox::up_button_rect() const {
    return tc_ui_rect {
        bounds().x + bounds().width - button_width_,
        bounds().y,
        button_width_,
        bounds().height * 0.5f
    };
}

tc_ui_rect SpinBox::down_button_rect() const {
    tc_ui_rect result = up_button_rect();
    result.y += result.height;
    return result;
}

tc_ui_size SpinBox::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    tc_ui_size result = preferred_size();
    if (measure_text(document, formatted_value(), style.font_size, metrics)) {
        result.width = std::max(result.width, metrics.width + button_width_ + 20.0f);
        result.height = std::max(result.height, metrics.line_height + 8.0f);
    }
    return clamp_size(result, constraints);
}

void SpinBox::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const std::string display = editing_ ? edit_text_ : formatted_value();
    tc_ui_text_metrics metrics {};
    measure_text(document, display, style.font_size, metrics);
    const float ascent = metrics.ascent > 0.0f ? metrics.ascent : style.font_size;
    const float line_height_value = metrics.line_height > 0.0f ? metrics.line_height : style.font_size;
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
    tc_ui_painter_fill_rect(context, up_button_rect(), style.border);
    tc_ui_painter_fill_rect(context, down_button_rect(), style.border);
    tc_ui_painter_draw_text(
        context, "+",
        tc_ui_point {up_button_rect().x + 5.0f, up_button_rect().y + up_button_rect().height - 3.0f},
        style.font_size, style.foreground
    );
    tc_ui_painter_draw_text(
        context, "-",
        tc_ui_point {down_button_rect().x + 6.0f, down_button_rect().y + down_button_rect().height - 3.0f},
        style.font_size, style.foreground
    );
    const tc_ui_rect clip {
        bounds().x + style.padding_left,
        bounds().y + style.padding_top,
        std::max(0.0f, bounds().width - button_width_ - style.padding_left - 2.0f),
        std::max(0.0f, bounds().height - style.padding_top - style.padding_bottom)
    };
    const float baseline = clip.y + std::max(0.0f, (clip.height - line_height_value) * 0.5f) + ascent;
    tc_ui_painter_push_clip(context, clip);
    tc_ui_painter_draw_text(context, display.c_str(), tc_ui_point {clip.x, baseline}, style.font_size, style.foreground);
    if (editing_ && tc_widget_handle_eq(tc_ui_document_focused_widget(document), handle())) {
        tc_ui_text_metrics prefix {};
        measure_text(document, std::string_view(edit_text_).substr(0, caret_), style.font_size, prefix);
        const float x = clip.x + prefix.width;
        tc_ui_painter_draw_line(context, tc_ui_point {x, clip.y + 3.0f}, tc_ui_point {x, clip.y + clip.height - 3.0f}, style.accent, 1.0f);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result SpinBox::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || event->type != TC_UI_POINTER_DOWN || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    tc_ui_document_set_focus(document, handle());
    if (rect_contains(up_button_rect(), event->x, event->y)) {
        set_value(value_ + step_);
    } else if (rect_contains(down_button_rect(), event->x, event->y)) {
        set_value(value_ - step_);
    } else {
        begin_edit();
    }
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result SpinBox::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN) return TC_UI_EVENT_IGNORED;
    if (event->key == TC_UI_KEY_UP_ARROW) {
        set_value(value_ + step_);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_DOWN_ARROW) {
        set_value(value_ - step_);
        return TC_UI_EVENT_HANDLED;
    }
    if (!editing_) return TC_UI_EVENT_IGNORED;
    switch (event->key) {
    case TC_UI_KEY_LEFT: if (caret_ > 0) --caret_; break;
    case TC_UI_KEY_RIGHT: if (caret_ < edit_text_.size()) ++caret_; break;
    case TC_UI_KEY_HOME: caret_ = 0; break;
    case TC_UI_KEY_END: caret_ = edit_text_.size(); break;
    case TC_UI_KEY_BACKSPACE:
        if (caret_ > 0) edit_text_.erase(--caret_, 1);
        break;
    case TC_UI_KEY_DELETE:
        if (caret_ < edit_text_.size()) edit_text_.erase(caret_, 1);
        break;
    case TC_UI_KEY_ENTER: commit_edit(); break;
    case TC_UI_KEY_ESCAPE: cancel_edit(); break;
    default: return TC_UI_EVENT_IGNORED;
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result SpinBox::text_event(tc_ui_document*, const tc_ui_text_event* event) {
    if (!event || !event->text || event->text[0] == '\0') return TC_UI_EVENT_IGNORED;
    if (!editing_) begin_edit();
    std::string filtered;
    for (const char ch : std::string_view(event->text)) {
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' || ch == 'e' || ch == 'E') {
            filtered.push_back(ch);
        }
    }
    if (!filtered.empty()) {
        edit_text_.insert(caret_, filtered);
        caret_ += filtered.size();
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }
    return TC_UI_EVENT_HANDLED;
}

void SpinBox::focus_event(tc_ui_document*, bool focused) {
    if (!focused) commit_edit();
}


} // namespace termin::gui_native
