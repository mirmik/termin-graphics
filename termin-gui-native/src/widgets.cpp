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

float primary_size(tc_ui_size size, Orientation orientation) {
    return orientation == Orientation::Vertical ? size.height : size.width;
}

float measured_or_fixed_extent(const LayoutItem& item, tc_ui_size measured, Orientation orientation) {
    if (item.policy == LayoutPolicy::Fixed && item.fixed_extent > 0.0f) {
        return item.fixed_extent;
    }
    return primary_size(measured, orientation);
}

float flexible_weight(const LayoutItem& item) {
    if (item.policy == LayoutPolicy::Flex) {
        return std::max(0.0f, item.flex);
    }
    if (item.policy == LayoutPolicy::Stretch) {
        return 1.0f;
    }
    return 0.0f;
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
    add_child(handle, LayoutPolicy::Stretch);
}

void BoxLayout::add_child(tc_widget_handle handle, LayoutPolicy policy, float value) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot add invalid child handle to BoxLayout");
        return;
    }

    LayoutItem item {};
    item.handle = handle;
    item.policy = policy;
    if (policy == LayoutPolicy::Fixed) {
        item.fixed_extent = std::max(0.0f, value);
    } else if (policy == LayoutPolicy::Flex) {
        item.flex = value > 0.0f ? value : 1.0f;
    }
    items_.push_back(item);
}

void BoxLayout::add_fixed_child(tc_widget_handle handle, float extent) {
    add_child(handle, LayoutPolicy::Fixed, extent);
}

void BoxLayout::add_preferred_child(tc_widget_handle handle) {
    add_child(handle, LayoutPolicy::Preferred);
}

void BoxLayout::add_flex_child(tc_widget_handle handle, float flex) {
    add_child(handle, LayoutPolicy::Flex, flex);
}

void BoxLayout::add_stretch_child(tc_widget_handle handle) {
    add_child(handle, LayoutPolicy::Stretch);
}

std::vector<tc_widget_handle> BoxLayout::children() const {
    std::vector<tc_widget_handle> handles;
    handles.reserve(items_.size());
    for (const LayoutItem& item : items_) {
        handles.push_back(item.handle);
    }
    return handles;
}

tc_ui_size BoxLayout::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size content {0.0f, 0.0f};
    size_t live_children = 0;
    for (const LayoutItem& item : items_) {
        tc_widget* child = resolve_child(document, item.handle, "BoxLayout::measure");
        if (!child) {
            continue;
        }
        tc_ui_size child_size = measure_widget(child, document, unconstrained());
        const float child_primary = measured_or_fixed_extent(item, child_size, orientation_);
        if (orientation_ == Orientation::Vertical) {
            content.width = std::max(content.width, child_size.width);
            content.height += child_primary;
        } else {
            content.width += child_primary;
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

    struct LiveItem {
        tc_widget* widget = nullptr;
        tc_ui_size measured {0.0f, 0.0f};
        float extent = 0.0f;
        float weight = 0.0f;
    };

    std::vector<LiveItem> live_items;
    live_items.reserve(items_.size());
    float reserved_extent = 0.0f;
    float total_weight = 0.0f;
    for (const LayoutItem& item : items_) {
        tc_widget* child = resolve_child(document, item.handle, "BoxLayout::layout");
        if (!child) {
            continue;
        }
        LiveItem live {};
        live.widget = child;
        live.measured = measure_widget(child, document, unconstrained());
        live.weight = flexible_weight(item);
        if (live.weight <= 0.0f) {
            live.extent = measured_or_fixed_extent(item, live.measured, orientation_);
            reserved_extent += live.extent;
        } else {
            total_weight += live.weight;
        }
        live_items.push_back(live);
    }
    if (live_items.empty()) {
        return;
    }

    tc_ui_rect content = inset_rect(rect, padding_);
    const float total_spacing = spacing_ * static_cast<float>(live_items.size() - 1);
    const float axis_extent = orientation_ == Orientation::Vertical ? content.height : content.width;
    const float flexible_extent = std::max(0.0f, axis_extent - total_spacing - reserved_extent);
    if (total_weight > 0.0f) {
        for (LiveItem& live : live_items) {
            if (live.weight > 0.0f) {
                live.extent = flexible_extent * (live.weight / total_weight);
            }
        }
    }

    float cursor = orientation_ == Orientation::Vertical ? content.y : content.x;
    for (const LiveItem& live : live_items) {
        tc_ui_rect child_rect {};
        if (orientation_ == Orientation::Vertical) {
            child_rect = tc_ui_rect {content.x, cursor, content.width, live.extent};
        } else {
            child_rect = tc_ui_rect {cursor, content.y, live.extent, content.height};
        }
        layout_widget(live.widget, document, child_rect);
        cursor += live.extent + spacing_;
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
    for (const LayoutItem& item : items_) {
        tc_widget* child = resolve_child(document, item.handle, "BoxLayout::paint");
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
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        tc_widget* child = tc_ui_document_resolve_widget(document, it->handle);
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
    for (const LayoutItem& item : items_) {
        visit(user_data, item.handle);
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

Button::Button(std::string text, Color fill)
    : NativeWidget("Button"), text_(std::move(text)), fill_(fill) {
    min_size_ = tc_ui_size {96.0f, 36.0f};
}

Button::Button(Color fill)
    : Button(std::string {}, fill) {}

Button& Button::set_accent(Color color) {
    accent_ = color;
    return *this;
}

Button& Button::set_text(std::string text) {
    text_ = std::move(text);
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
    if (!text_.empty()) {
        tc_ui_painter_draw_text(
            context,
            text_.c_str(),
            tc_ui_point {bounds_.x + 12.0f, bounds_.y + bounds_.height * 0.5f + 5.0f},
            14.0f,
            tc_ui_color {0.94f, 0.97f, 1.0f, 1.0f}
        );
    }
}

Label::Label(std::string text, float font_size, Color color)
    : NativeWidget("Label"),
      text_(std::move(text)),
      font_size_(std::max(1.0f, font_size)),
      color_(color) {
    update_min_size();
}

Label& Label::set_text(std::string text) {
    text_ = std::move(text);
    update_min_size();
    return *this;
}

Label& Label::set_color(Color color) {
    color_ = color;
    return *this;
}

Label& Label::set_font_size(float font_size) {
    font_size_ = std::max(1.0f, font_size);
    update_min_size();
    return *this;
}

void Label::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_draw_text(
        context,
        text_.c_str(),
        tc_ui_point {bounds_.x, bounds_.y + font_size_},
        font_size_,
        color_.c_color()
    );
}

void Label::update_min_size() {
    min_size_ = tc_ui_size {
        std::max(1.0f, static_cast<float>(text_.size()) * font_size_ * 0.56f),
        font_size_ * 1.35f
    };
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
