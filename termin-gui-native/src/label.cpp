#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

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

tc_ui_size Label::measure(tc_ui_document_handle document, tc_ui_constraints constraints) {
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

void Label::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
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


} // namespace termin::gui_native
