#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

SliderEdit::SliderEdit(float value)
    : NativeWidget("SliderEdit"), value_(value) {
    set_preferred_size(tc_ui_size {300.0f, 34.0f});
}

void SliderEdit::set_value(float value) {
    if (!std::isfinite(value)) {
        tc_log_error("[termin-gui-native] SliderEdit rejected non-finite value");
        return;
    }
    const float next = clamp_float(value, min_value_, max_value_);
    if (std::fabs(next - value_) <= 0.000001f) return;
    value_ = next;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, value_);
    if (!syncing_ && document() &&
        tc_ui_document_is_alive(document(), slider_handle_) &&
        tc_ui_document_is_alive(document(), spin_box_handle_)) {
        sync_children(document());
    }
}

void SliderEdit::set_range(float min_value, float max_value) {
    if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value < min_value) {
        tc_log_error("[termin-gui-native] SliderEdit rejected invalid range");
        return;
    }
    min_value_ = min_value;
    max_value_ = max_value;
    set_value(value_);
    if (!syncing_ && document() &&
        tc_ui_document_is_alive(document(), slider_handle_) &&
        tc_ui_document_is_alive(document(), spin_box_handle_)) {
        sync_children(document());
    }
}

void SliderEdit::set_step(float step) {
    if (!std::isfinite(step) || step < 0.0f) {
        tc_log_error("[termin-gui-native] SliderEdit rejected invalid step");
        return;
    }
    step_ = step;
    if (!syncing_ && document() &&
        tc_ui_document_is_alive(document(), slider_handle_) &&
        tc_ui_document_is_alive(document(), spin_box_handle_)) {
        sync_children(document());
    }
}

void SliderEdit::set_decimals(int decimals) {
    if (decimals < 0 || decimals > 9) {
        tc_log_error("[termin-gui-native] SliderEdit rejected invalid decimals");
        return;
    }
    decimals_ = decimals;
    if (!syncing_ && document() && tc_ui_document_is_alive(document(), spin_box_handle_)) {
        sync_children(document());
    }
}

void SliderEdit::set_label(std::string label) {
    label_ = std::move(label);
    set_preferred_size(tc_ui_size {300.0f, label_.empty() ? 34.0f : 52.0f});
}

bool SliderEdit::ensure_children(tc_ui_document* document) {
    if (!tc_widget_handle_is_invalid(slider_handle_) && !tc_widget_handle_is_invalid(spin_box_handle_)) {
        return true;
    }
    auto slider = std::make_unique<Slider>(value_);
    auto spin_box = std::make_unique<SpinBox>(value_);
    slider_handle_ = tc_ui_document_adopt_widget(
        document, slider->c_widget(), &Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(slider_handle_)) return false;
    slider.release();
    spin_box_handle_ = tc_ui_document_adopt_widget(
        document, spin_box->c_widget(), &Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(spin_box_handle_)) {
        tc_ui_document_destroy_widget(document, slider_handle_);
        slider_handle_ = tc_widget_handle_invalid();
        return false;
    }
    spin_box.release();
    tc_widget* slider_widget = tc_ui_document_resolve_widget(document, slider_handle_);
    tc_widget* spin_widget = tc_ui_document_resolve_widget(document, spin_box_handle_);
    if (!tc_widget_append_child(c_widget(), slider_widget) || !tc_widget_append_child(c_widget(), spin_widget)) {
        tc_log_error("[termin-gui-native] SliderEdit failed to attach numeric children");
        return false;
    }
    auto* slider_body = static_cast<Slider*>(slider_widget->body);
    auto* spin_body = static_cast<SpinBox*>(spin_widget->body);
    slider_connection_ = slider_body->changed().connect([this, spin_body](Slider&, float value) {
        if (syncing_) return;
        syncing_ = true;
        set_value(value);
        spin_body->set_value(value_);
        syncing_ = false;
    });
    spin_box_connection_ = spin_body->changed().connect([this, slider_body](SpinBox&, float value) {
        if (syncing_) return;
        syncing_ = true;
        set_value(value);
        slider_body->set_value(value_);
        syncing_ = false;
    });
    return true;
}

void SliderEdit::sync_children(tc_ui_document* document) {
    auto* slider = static_cast<Slider*>(tc_ui_document_resolve_widget(document, slider_handle_)->body);
    auto* spin_box = static_cast<SpinBox*>(tc_ui_document_resolve_widget(document, spin_box_handle_)->body);
    syncing_ = true;
    slider->set_range(min_value_, max_value_);
    slider->set_step(step_);
    slider->set_value(value_);
    spin_box->set_range(min_value_, max_value_);
    spin_box->set_step(step_ > 0.0f ? step_ : 0.01f);
    spin_box->set_decimals(decimals_);
    spin_box->set_value(value_);
    syncing_ = false;
}

tc_ui_size SliderEdit::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return clamp_size(preferred_size(), constraints);
}

void SliderEdit::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (!ensure_children(document)) return;
    sync_children(document);
    const float label_height = label_.empty() ? 0.0f : 18.0f;
    const float content_y = rect.y + label_height;
    const float content_height = std::max(0.0f, rect.height - label_height);
    const float slider_width = std::max(0.0f, rect.width - spin_box_width_ - spacing_);
    tc_widget_set_bounds(
        tc_ui_document_resolve_widget(document, slider_handle_),
        tc_ui_rect {rect.x, content_y, slider_width, content_height}
    );
    tc_widget_set_bounds(
        tc_ui_document_resolve_widget(document, spin_box_handle_),
        tc_ui_rect {rect.x + slider_width + spacing_, content_y, spin_box_width_, content_height}
    );
}

void SliderEdit::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    if (label_.empty()) return;
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_draw_text(context, label_.c_str(), tc_ui_point {bounds().x, bounds().y + 13.0f}, 11.0f, style.foreground);
}

void SliderEdit::on_destroy(tc_ui_document* document) {
    if (tc_widget* widget = tc_ui_document_resolve_widget(document, slider_handle_)) {
        static_cast<Slider*>(widget->body)->changed().disconnect(slider_connection_);
    }
    if (tc_widget* widget = tc_ui_document_resolve_widget(document, spin_box_handle_)) {
        static_cast<SpinBox*>(widget->body)->changed().disconnect(spin_box_connection_);
    }
}


} // namespace termin::gui_native
