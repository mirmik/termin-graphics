#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
const tc_widget_vtable NativeWidget::VTABLE {
    "NativeWidget",
    &NativeWidget::dispatch_measure,
    &NativeWidget::dispatch_layout,
    &NativeWidget::dispatch_paint,
    &NativeWidget::dispatch_pointer_event,
    &NativeWidget::dispatch_hit_test,
    &NativeWidget::dispatch_key_event,
    &NativeWidget::dispatch_text_event,
    &NativeWidget::dispatch_focus_event,
    &NativeWidget::dispatch_overlay_dismissed,
    &NativeWidget::dispatch_on_destroy,
};

NativeWidget::NativeWidget(const char* debug_name)
    : Widget(&VTABLE, debug_name) {}

tc_ui_style NativeWidget::computed_style(
    tc_ui_document* document,
    uint32_t extra_state_flags
) const {
    tc_ui_style style {};
    if (!tc_ui_document_resolve_style(document, c_widget(), extra_state_flags, &style)) {
        throw std::runtime_error("failed to resolve native UI widget style");
    }
    return style;
}

tc_ui_size NativeWidget::measure(tc_ui_document*, tc_ui_constraints constraints) {
    const float constraint_max_width = effective_max(constraints.max_size.width);
    const float constraint_max_height = effective_max(constraints.max_size.height);
    const float max_width = std::min(effective_max(max_size().width), constraint_max_width);
    const float max_height = std::min(effective_max(max_size().height), constraint_max_height);
    const float min_width = std::max(min_size().width, constraints.min_size.width);
    const float min_height = std::max(min_size().height, constraints.min_size.height);
    const tc_ui_constraints effective_constraints {
        tc_ui_size {min_width, min_height},
        tc_ui_size {std::max(min_width, max_width), std::max(min_height, max_height)}
    };
    const tc_ui_size preferred {
        preferred_size().width > 0.0f ? preferred_size().width : min_size().width,
        preferred_size().height > 0.0f ? preferred_size().height : min_size().height
    };
    return clamp_size(preferred, effective_constraints);
}

void NativeWidget::layout(tc_ui_document*, tc_ui_rect rect) {
    tc_widget_set_bounds(c_widget(), rect);
    clear_dirty(TC_WIDGET_DIRTY_LAYOUT);
}

void NativeWidget::paint(tc_ui_document*, tc_ui_paint_context*) {}

tc_ui_event_result NativeWidget::pointer_event(tc_ui_document*, const tc_ui_pointer_event*) {
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle NativeWidget::hit_test(tc_ui_document*, float x, float y) {
    if (!visible() || !rect_contains(bounds(), x, y) || mouse_transparent()) {
        return tc_widget_handle_invalid();
    }
    return handle();
}

tc_ui_event_result NativeWidget::key_event(tc_ui_document*, const tc_ui_key_event*) {
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result NativeWidget::text_event(tc_ui_document*, const tc_ui_text_event*) {
    return TC_UI_EVENT_IGNORED;
}

void NativeWidget::focus_event(tc_ui_document*, bool) {}

void NativeWidget::overlay_dismissed(tc_ui_document*, tc_ui_overlay_dismiss_reason) {}

void NativeWidget::on_destroy(tc_ui_document*) {}

tc_ui_size NativeWidget::dispatch_measure(
    tc_widget* widget,
    tc_ui_document* document,
    tc_ui_constraints constraints
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    return self ? self->measure(document, constraints) : constraints.min_size;
}

void NativeWidget::dispatch_layout(tc_widget* widget, tc_ui_document* document, tc_ui_rect rect) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    if (self) {
        self->layout(document, rect);
    }
}

void NativeWidget::dispatch_paint(
    tc_widget* widget,
    tc_ui_document* document,
    tc_ui_paint_context* context
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    if (self) {
        self->paint(document, context);
    }
}

tc_ui_event_result NativeWidget::dispatch_pointer_event(
    tc_widget* widget,
    tc_ui_document* document,
    const tc_ui_pointer_event* event
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    return self ? self->pointer_event(document, event) : TC_UI_EVENT_IGNORED;
}

tc_widget_handle NativeWidget::dispatch_hit_test(
    tc_widget* widget,
    tc_ui_document* document,
    float x,
    float y
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    return self ? self->hit_test(document, x, y) : tc_widget_handle_invalid();
}

tc_ui_event_result NativeWidget::dispatch_key_event(
    tc_widget* widget,
    tc_ui_document* document,
    const tc_ui_key_event* event
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    return self ? self->key_event(document, event) : TC_UI_EVENT_IGNORED;
}

tc_ui_event_result NativeWidget::dispatch_text_event(
    tc_widget* widget,
    tc_ui_document* document,
    const tc_ui_text_event* event
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    return self ? self->text_event(document, event) : TC_UI_EVENT_IGNORED;
}

void NativeWidget::dispatch_focus_event(
    tc_widget* widget,
    tc_ui_document* document,
    bool focused
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    if (self) {
        self->focus_event(document, focused);
    }
}

void NativeWidget::dispatch_overlay_dismissed(
    tc_widget* widget,
    tc_ui_document* document,
    tc_ui_overlay_dismiss_reason reason
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    if (self) {
        self->overlay_dismissed(document, reason);
    }
}

void NativeWidget::dispatch_on_destroy(tc_widget* widget, tc_ui_document* document) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    if (self) {
        self->on_destroy(document);
    }
}

} // namespace termin::gui_native
