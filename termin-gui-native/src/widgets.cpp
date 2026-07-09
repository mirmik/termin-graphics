#include <termin/gui_native/widgets.hpp>

#include <algorithm>
#include <cmath>

#include <tcbase/tc_log.h>

namespace termin::gui_native {
namespace {

constexpr float kHuge = 1000000.0f;

float clamp_float(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

bool color_visible(Color color) {
    return color.a > 0.0f;
}

bool rect_contains(tc_ui_rect rect, float x, float y) {
    return x >= rect.x && y >= rect.y &&
           x <= rect.x + rect.width &&
           y <= rect.y + rect.height;
}

tc_ui_size clamp_size(tc_ui_size size, tc_ui_constraints constraints) {
    const float max_width = constraints.max_size.width > 0.0f ? constraints.max_size.width : kHuge;
    const float max_height = constraints.max_size.height > 0.0f ? constraints.max_size.height : kHuge;
    return tc_ui_size {
        clamp_float(size.width, constraints.min_size.width, max_width),
        clamp_float(size.height, constraints.min_size.height, max_height)
    };
}

tc_ui_constraints unconstrained() {
    return tc_ui_constraints {
        tc_ui_size {0.0f, 0.0f},
        tc_ui_size {kHuge, kHuge}
    };
}

tc_widget* resolve_child(tc_ui_document* document, tc_widget_handle handle, const char* owner) {
    tc_widget* child = tc_ui_document_resolve_widget(document, handle);
    if (!child) {
        tc_log_error(
            "[termin-gui-native] %s skipped stale child handle index=%u generation=%u",
            owner ? owner : "widget",
            handle.index,
            handle.generation
        );
    }
    return child;
}

tc_ui_size measure_widget(tc_widget* widget, tc_ui_document* document, tc_ui_constraints constraints) {
    if (widget && widget->vtable && widget->vtable->measure) {
        return widget->vtable->measure(widget, document, constraints);
    }
    return constraints.min_size;
}

void layout_widget(tc_widget* widget, tc_ui_document* document, tc_ui_rect rect) {
    if (widget && widget->vtable && widget->vtable->layout) {
        widget->vtable->layout(widget, document, rect);
    }
}

void paint_widget(tc_widget* widget, tc_ui_document* document, tc_ui_paint_context* context) {
    if (widget && widget->vtable && widget->vtable->paint) {
        widget->vtable->paint(widget, document, context);
    }
}

tc_ui_rect inset_rect(tc_ui_rect rect, EdgeInsets padding) {
    rect.x += padding.left;
    rect.y += padding.top;
    rect.width = std::max(0.0f, rect.width - padding.left - padding.right);
    rect.height = std::max(0.0f, rect.height - padding.top - padding.bottom);
    return rect;
}

} // namespace

const tc_widget_vtable NativeWidget::VTABLE {
    "NativeWidget",
    &NativeWidget::dispatch_measure,
    &NativeWidget::dispatch_layout,
    &NativeWidget::dispatch_paint,
    &NativeWidget::dispatch_pointer_event,
    &NativeWidget::dispatch_visit_recursive_destroy_targets,
    &NativeWidget::dispatch_on_destroy,
};

NativeWidget::NativeWidget(const char* debug_name)
    : Widget(&VTABLE, debug_name) {}

tc_ui_size NativeWidget::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return clamp_size(min_size_, constraints);
}

void NativeWidget::layout(tc_ui_document*, tc_ui_rect rect) {
    bounds_ = rect;
}

void NativeWidget::paint(tc_ui_document*, tc_ui_paint_context*) {}

tc_ui_event_result NativeWidget::pointer_event(tc_ui_document*, const tc_ui_pointer_event*) {
    return TC_UI_EVENT_IGNORED;
}

void NativeWidget::visit_recursive_destroy_targets(
    tc_ui_document*,
    void*,
    tc_widget_visit_fn
) {}

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

void NativeWidget::dispatch_visit_recursive_destroy_targets(
    tc_widget* widget,
    tc_ui_document* document,
    void* user_data,
    tc_widget_visit_fn visit
) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    if (self) {
        self->visit_recursive_destroy_targets(document, user_data, visit);
    }
}

void NativeWidget::dispatch_on_destroy(tc_widget* widget, tc_ui_document* document) {
    auto* self = static_cast<NativeWidget*>(widget ? widget->body : nullptr);
    if (self) {
        self->on_destroy(document);
    }
}

BoxLayout::BoxLayout(Orientation orientation, const char* debug_name)
    : NativeWidget(debug_name), orientation_(orientation) {}

BoxLayout& BoxLayout::set_padding(EdgeInsets padding) {
    padding_ = padding;
    return *this;
}

BoxLayout& BoxLayout::set_spacing(float spacing) {
    spacing_ = std::max(0.0f, spacing);
    return *this;
}

BoxLayout& BoxLayout::set_background(Color color) {
    background_ = color;
    return *this;
}

BoxLayout& BoxLayout::set_border(Color color, float thickness) {
    border_ = color;
    border_thickness_ = std::max(0.0f, thickness);
    return *this;
}

void BoxLayout::add_child(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot add invalid child handle to BoxLayout");
        return;
    }
    children_.push_back(handle);
}

tc_ui_size BoxLayout::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size content {0.0f, 0.0f};
    size_t live_children = 0;
    for (tc_widget_handle handle : children_) {
        tc_widget* child = resolve_child(document, handle, "BoxLayout::measure");
        if (!child) {
            continue;
        }
        tc_ui_size child_size = measure_widget(child, document, unconstrained());
        if (orientation_ == Orientation::Vertical) {
            content.width = std::max(content.width, child_size.width);
            content.height += child_size.height;
        } else {
            content.width += child_size.width;
            content.height = std::max(content.height, child_size.height);
        }
        live_children += 1;
    }

    if (live_children > 1) {
        const float total_spacing = spacing_ * static_cast<float>(live_children - 1);
        if (orientation_ == Orientation::Vertical) {
            content.height += total_spacing;
        } else {
            content.width += total_spacing;
        }
    }

    content.width += padding_.left + padding_.right;
    content.height += padding_.top + padding_.bottom;
    content.width = std::max(content.width, min_size_.width);
    content.height = std::max(content.height, min_size_.height);
    return clamp_size(content, constraints);
}

void BoxLayout::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);

    std::vector<tc_widget*> live_children;
    live_children.reserve(children_.size());
    for (tc_widget_handle handle : children_) {
        if (tc_widget* child = resolve_child(document, handle, "BoxLayout::layout")) {
            live_children.push_back(child);
        }
    }
    if (live_children.empty()) {
        return;
    }

    tc_ui_rect content = inset_rect(rect, padding_);
    const float total_spacing = spacing_ * static_cast<float>(live_children.size() - 1);
    const float axis_extent = orientation_ == Orientation::Vertical ? content.height : content.width;
    const float child_extent = std::max(
        0.0f,
        (axis_extent - total_spacing) / static_cast<float>(live_children.size())
    );

    float cursor = orientation_ == Orientation::Vertical ? content.y : content.x;
    for (tc_widget* child : live_children) {
        tc_ui_rect child_rect {};
        if (orientation_ == Orientation::Vertical) {
            child_rect = tc_ui_rect {content.x, cursor, content.width, child_extent};
        } else {
            child_rect = tc_ui_rect {cursor, content.y, child_extent, content.height};
        }
        layout_widget(child, document, child_rect);
        cursor += child_extent + spacing_;
    }
}

void BoxLayout::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    if (color_visible(background_)) {
        tc_ui_painter_fill_rect(context, bounds_, background_.c_color());
    }
    if (color_visible(border_) && border_thickness_ > 0.0f) {
        tc_ui_painter_stroke_rect(context, bounds_, border_.c_color(), border_thickness_);
    }

    tc_ui_painter_push_clip(context, bounds_);
    for (tc_widget_handle handle : children_) {
        tc_widget* child = resolve_child(document, handle, "BoxLayout::paint");
        paint_widget(child, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result BoxLayout::pointer_event(
    tc_ui_document* document,
    const tc_ui_pointer_event* event
) {
    if (!event || !rect_contains(bounds_, event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        tc_widget* child = tc_ui_document_resolve_widget(document, *it);
        if (!child || !child->vtable || !child->vtable->pointer_event) {
            continue;
        }
        if (child->vtable->pointer_event(child, document, event) == TC_UI_EVENT_HANDLED) {
            return TC_UI_EVENT_HANDLED;
        }
    }
    return TC_UI_EVENT_IGNORED;
}

void BoxLayout::visit_recursive_destroy_targets(
    tc_ui_document*,
    void* user_data,
    tc_widget_visit_fn visit
) {
    if (!visit) {
        return;
    }
    for (tc_widget_handle child : children_) {
        visit(user_data, child);
    }
}

Panel::Panel(const char* debug_name)
    : NativeWidget(debug_name) {
    min_size_ = tc_ui_size {96.0f, 64.0f};
}

Panel& Panel::set_fill(Color color) {
    fill_ = color;
    return *this;
}

Panel& Panel::set_border(Color color, float thickness) {
    border_ = color;
    border_thickness_ = std::max(0.0f, thickness);
    return *this;
}

void Panel::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds_, fill_.c_color());
    if (border_thickness_ > 0.0f && color_visible(border_)) {
        tc_ui_painter_stroke_rect(context, bounds_, border_.c_color(), border_thickness_);
    }
}

Button::Button(Color fill)
    : NativeWidget("Button"), fill_(fill) {
    min_size_ = tc_ui_size {96.0f, 36.0f};
}

Button& Button::set_accent(Color color) {
    accent_ = color;
    return *this;
}

void Button::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds_, fill_.c_color());
    tc_ui_painter_stroke_rect(context, bounds_, accent_.c_color(), 2.0f);
    const float y = bounds_.y + bounds_.height * 0.5f;
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {bounds_.x + 14.0f, y},
        tc_ui_point {bounds_.x + bounds_.width - 14.0f, y},
        accent_.c_color(),
        2.0f
    );
}

Checkbox::Checkbox(bool checked)
    : NativeWidget("Checkbox"), checked_(checked) {
    min_size_ = tc_ui_size {32.0f, 32.0f};
}

void Checkbox::paint(tc_ui_document*, tc_ui_paint_context* context) {
    const float side = std::min(bounds_.width, bounds_.height);
    const tc_ui_rect box {
        bounds_.x + (bounds_.width - side) * 0.5f,
        bounds_.y + (bounds_.height - side) * 0.5f,
        side,
        side
    };
    tc_ui_painter_fill_rect(context, box, tc_ui_color {0.10f, 0.11f, 0.13f, 1.0f});
    tc_ui_painter_stroke_rect(context, box, tc_ui_color {0.74f, 0.78f, 0.84f, 1.0f}, 2.0f);
    if (checked_) {
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {box.x + side * 0.22f, box.y + side * 0.55f},
            tc_ui_point {box.x + side * 0.43f, box.y + side * 0.74f},
            tc_ui_color {0.28f, 0.82f, 0.54f, 1.0f},
            3.0f
        );
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {box.x + side * 0.43f, box.y + side * 0.74f},
            tc_ui_point {box.x + side * 0.78f, box.y + side * 0.26f},
            tc_ui_color {0.28f, 0.82f, 0.54f, 1.0f},
            3.0f
        );
    }
}

tc_ui_event_result Checkbox::pointer_event(tc_ui_document*, const tc_ui_pointer_event* event) {
    if (!event || event->type != TC_UI_POINTER_DOWN || !rect_contains(bounds_, event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    checked_ = !checked_;
    return TC_UI_EVENT_HANDLED;
}

ProgressBar::ProgressBar(float value)
    : NativeWidget("ProgressBar") {
    min_size_ = tc_ui_size {120.0f, 20.0f};
    set_value(value);
}

void ProgressBar::set_value(float value) {
    value_ = clamp_float(value, 0.0f, 1.0f);
}

void ProgressBar::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds_, tc_ui_color {0.09f, 0.10f, 0.12f, 1.0f});
    tc_ui_rect fill = bounds_;
    fill.width *= value_;
    tc_ui_painter_fill_rect(context, fill, tc_ui_color {0.25f, 0.58f, 0.88f, 1.0f});
    tc_ui_painter_stroke_rect(context, bounds_, tc_ui_color {0.38f, 0.42f, 0.48f, 1.0f}, 1.0f);
}

Slider::Slider(float value)
    : NativeWidget("Slider") {
    min_size_ = tc_ui_size {140.0f, 28.0f};
    set_value(value);
}

void Slider::set_value(float value) {
    value_ = clamp_float(value, 0.0f, 1.0f);
}

void Slider::paint(tc_ui_document*, tc_ui_paint_context* context) {
    const float center_y = bounds_.y + bounds_.height * 0.5f;
    const float left = bounds_.x + 10.0f;
    const float right = bounds_.x + bounds_.width - 10.0f;
    const float knob_x = left + (right - left) * value_;
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {left, center_y},
        tc_ui_point {right, center_y},
        tc_ui_color {0.32f, 0.34f, 0.38f, 1.0f},
        4.0f
    );
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {left, center_y},
        tc_ui_point {knob_x, center_y},
        tc_ui_color {0.88f, 0.66f, 0.24f, 1.0f},
        4.0f
    );
    tc_ui_painter_fill_rect(
        context,
        tc_ui_rect {knob_x - 5.0f, center_y - 10.0f, 10.0f, 20.0f},
        tc_ui_color {0.96f, 0.88f, 0.64f, 1.0f}
    );
}

tc_ui_event_result Slider::pointer_event(tc_ui_document*, const tc_ui_pointer_event* event) {
    if (!event || !rect_contains(bounds_, event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type != TC_UI_POINTER_DOWN && event->type != TC_UI_POINTER_MOVE) {
        return TC_UI_EVENT_IGNORED;
    }
    const float left = bounds_.x + 10.0f;
    const float right = bounds_.x + bounds_.width - 10.0f;
    if (right <= left) {
        return TC_UI_EVENT_HANDLED;
    }
    set_value((event->x - left) / (right - left));
    return TC_UI_EVENT_HANDLED;
}

Swatch::Swatch(Color color)
    : NativeWidget("Swatch"), color_(color) {
    min_size_ = tc_ui_size {36.0f, 36.0f};
}

void Swatch::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds_, color_.c_color());
    tc_ui_painter_stroke_rect(context, bounds_, tc_ui_color {0.88f, 0.90f, 0.94f, 1.0f}, 1.0f);
}

Spacer::Spacer(tc_ui_size size)
    : NativeWidget("Spacer") {
    min_size_ = size;
}

} // namespace termin::gui_native
