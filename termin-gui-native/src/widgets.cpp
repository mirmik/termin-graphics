#include <termin/gui_native/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

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

float effective_max(float value) {
    return value > 0.0f ? value : kHuge;
}

tc_ui_constraints unconstrained() {
    return tc_ui_constraints {
        tc_ui_size {0.0f, 0.0f},
        tc_ui_size {kHuge, kHuge}
    };
}

tc_widget* resolve_child(
    tc_ui_document* document,
    const tc_widget* expected_parent,
    tc_widget_handle handle,
    const char* owner
) {
    (void)owner;
    if (!document || !expected_parent) {
        return nullptr;
    }
    for (size_t index = 0; index < expected_parent->child_count; ++index) {
        tc_widget* child = expected_parent->children[index];
        if (child && tc_widget_handle_eq(child->handle, handle) &&
            child->document == document && tc_widget_is_visible(child)) {
            return child;
        }
    }
    return nullptr;
}

tc_widget* attach_child(
    tc_widget* parent,
    tc_widget_handle child_handle,
    size_t index,
    const char* owner
) {
    if (!parent || !parent->document) {
        tc_log_error("[termin-gui-native] %s has not been adopted", owner ? owner : "widget");
        return nullptr;
    }
    tc_widget* child = tc_ui_document_resolve_widget(parent->document, child_handle);
    if (!child) {
        tc_log_error("[termin-gui-native] %s cannot resolve child handle", owner ? owner : "widget");
        return nullptr;
    }
    if (!tc_widget_insert_child(parent, index, child)) {
        tc_log_error("[termin-gui-native] %s failed to attach child", owner ? owner : "widget");
        return nullptr;
    }
    return child;
}

void detach_if_child(tc_widget* parent, tc_widget_handle child_handle) {
    if (!parent || !parent->document || tc_widget_handle_is_invalid(child_handle)) {
        return;
    }
    tc_widget* child = tc_ui_document_resolve_widget(parent->document, child_handle);
    if (child && child->parent == parent) {
        tc_widget_remove_child(parent, child);
    }
}

tc_ui_size measure_widget(tc_widget* widget, tc_ui_document* document, tc_ui_constraints constraints) {
    if (widget && widget->vtable && widget->vtable->measure) {
        return widget->vtable->measure(widget, document, constraints);
    }
    return constraints.min_size;
}

NativeWidget* native_widget_body(tc_widget* widget) {
    if (!widget || widget->native_language != TC_LANGUAGE_CXX || !widget->body) {
        return nullptr;
    }
    return dynamic_cast<NativeWidget*>(static_cast<Widget*>(widget->body));
}

void layout_widget(tc_widget* widget, tc_ui_document* document, tc_ui_rect rect) {
    if (widget && tc_widget_is_visible(widget) && widget->vtable && widget->vtable->layout) {
        widget->vtable->layout(widget, document, rect);
    }
}

void paint_widget(tc_widget* widget, tc_ui_document* document, tc_ui_paint_context* context) {
    if (widget && tc_widget_is_visible(widget) && widget->vtable && widget->vtable->paint) {
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

float cross_size(tc_ui_size size, Orientation orientation) {
    return orientation == Orientation::Vertical ? size.width : size.height;
}

float item_basis(const LayoutItem& item, tc_ui_size measured, Orientation orientation) {
    if (item.policy == LayoutPolicy::Fixed && item.fixed_extent > 0.0f) {
        return item.fixed_extent;
    }
    return primary_size(measured, orientation);
}

float item_min_extent(const LayoutItem& item, const NativeWidget* native, Orientation orientation) {
    if (item.policy == LayoutPolicy::Fixed && item.fixed_extent > 0.0f) {
        return item.fixed_extent;
    }
    if (item.min_extent > 0.0f) {
        return item.min_extent;
    }
    return native ? primary_size(native->min_size(), orientation) : 0.0f;
}

float item_max_extent(const LayoutItem& item, const NativeWidget* native, Orientation orientation) {
    if (item.policy == LayoutPolicy::Fixed && item.fixed_extent > 0.0f) {
        return item.fixed_extent;
    }
    if (item.max_extent > 0.0f) {
        return item.max_extent;
    }
    return native ? effective_max(primary_size(native->max_size(), orientation)) : kHuge;
}

void distribute_grow(std::vector<float>& extents, const std::vector<float>& max_extents, const std::vector<float>& weights, float extra) {
    float remaining = extra;
    constexpr float kEpsilon = 0.0001f;
    while (remaining > kEpsilon) {
        float total_weight = 0.0f;
        for (size_t i = 0; i < extents.size(); ++i) {
            if (weights[i] > 0.0f && extents[i] < max_extents[i] - kEpsilon) {
                total_weight += weights[i];
            }
        }
        if (total_weight <= 0.0f) {
            return;
        }

        float distributed = 0.0f;
        for (size_t i = 0; i < extents.size(); ++i) {
            if (weights[i] <= 0.0f || extents[i] >= max_extents[i] - kEpsilon) {
                continue;
            }
            const float share = remaining * (weights[i] / total_weight);
            const float next = std::min(max_extents[i], extents[i] + share);
            distributed += next - extents[i];
            extents[i] = next;
        }
        if (distributed <= kEpsilon) {
            return;
        }
        remaining -= distributed;
    }
}

void distribute_shrink(std::vector<float>& extents, const std::vector<float>& min_extents, const std::vector<float>& weights, float deficit) {
    float remaining = deficit;
    constexpr float kEpsilon = 0.0001f;
    while (remaining > kEpsilon) {
        float total_weight = 0.0f;
        for (size_t i = 0; i < extents.size(); ++i) {
            if (weights[i] > 0.0f && extents[i] > min_extents[i] + kEpsilon) {
                total_weight += weights[i];
            }
        }
        if (total_weight <= 0.0f) {
            return;
        }

        float distributed = 0.0f;
        for (size_t i = 0; i < extents.size(); ++i) {
            if (weights[i] <= 0.0f || extents[i] <= min_extents[i] + kEpsilon) {
                continue;
            }
            const float share = remaining * (weights[i] / total_weight);
            const float next = std::max(min_extents[i], extents[i] - share);
            distributed += extents[i] - next;
            extents[i] = next;
        }
        if (distributed <= kEpsilon) {
            return;
        }
        remaining -= distributed;
    }
}

GridTrack make_grid_track(LayoutPolicy policy, float value) {
    GridTrack track {};
    track.policy = policy;
    track.value = std::max(0.0f, value);
    if (policy == LayoutPolicy::Fixed) {
        track.grow = 0.0f;
        track.shrink = 0.0f;
        track.min_extent = track.value;
        track.max_extent = track.value;
    } else if (policy == LayoutPolicy::Preferred) {
        track.grow = 0.0f;
        track.shrink = 0.0f;
        track.min_extent = track.value;
    } else if (policy == LayoutPolicy::Flex) {
        track.grow = track.value > 0.0f ? track.value : 1.0f;
        track.shrink = track.grow;
    } else {
        track.grow = 1.0f;
        track.shrink = 1.0f;
    }
    return track;
}

void set_track_limits(GridTrack& track, float min_extent, float max_extent) {
    track.min_extent = std::max(0.0f, min_extent);
    track.max_extent = std::max(0.0f, max_extent);
    if (track.policy == LayoutPolicy::Fixed) {
        track.min_extent = track.value;
        track.max_extent = track.value;
    } else if (track.max_extent > 0.0f && track.max_extent < track.min_extent) {
        track.max_extent = track.min_extent;
    }
}

const LayoutItem* find_layout_item(
    const std::vector<LayoutItem>& items,
    tc_widget_handle handle
) {
    const auto found = std::find_if(
        items.begin(),
        items.end(),
        [handle](const LayoutItem& item) { return tc_widget_handle_eq(item.handle, handle); }
    );
    return found != items.end() ? &*found : nullptr;
}

const TabPage* find_tab_page(const std::vector<TabPage>& pages, tc_widget_handle handle) {
    const auto found = std::find_if(
        pages.begin(),
        pages.end(),
        [handle](const TabPage& page) { return tc_widget_handle_eq(page.handle, handle); }
    );
    return found != pages.end() ? &*found : nullptr;
}

float track_base_extent(const GridTrack& track) {
    if (track.policy == LayoutPolicy::Fixed || track.policy == LayoutPolicy::Preferred) {
        return track.value;
    }
    return track.min_extent;
}

float track_max_extent(const GridTrack& track) {
    return track.max_extent > 0.0f ? track.max_extent : kHuge;
}

void apply_span_requirement(
    std::vector<float>& extents,
    const std::vector<float>& max_extents,
    size_t start,
    size_t span,
    float spacing,
    float required_extent
) {
    if (extents.empty() || start >= extents.size()) {
        return;
    }
    const size_t end = std::min(extents.size(), start + std::max<size_t>(1, span));
    float current = spacing * static_cast<float>(end - start - 1);
    for (size_t i = start; i < end; ++i) {
        current += extents[i];
    }
    float deficit = required_extent - current;
    constexpr float kEpsilon = 0.0001f;
    while (deficit > kEpsilon) {
        size_t growable_count = 0;
        for (size_t i = start; i < end; ++i) {
            if (extents[i] < max_extents[i] - kEpsilon) {
                growable_count += 1;
            }
        }
        if (growable_count == 0) {
            return;
        }

        float distributed = 0.0f;
        const float share = deficit / static_cast<float>(growable_count);
        for (size_t i = start; i < end; ++i) {
            if (extents[i] >= max_extents[i] - kEpsilon) {
                continue;
            }
            const float next = std::min(max_extents[i], extents[i] + share);
            distributed += next - extents[i];
            extents[i] = next;
        }
        if (distributed <= kEpsilon) {
            return;
        }
        deficit -= distributed;
    }
}

struct GridAxisLayout {
    std::vector<float> extents;
    std::vector<float> min_extents;
    std::vector<float> max_extents;
    std::vector<float> grow_weights;
    std::vector<float> shrink_weights;
};

GridAxisLayout build_grid_axis(
    tc_ui_document* document,
    const tc_widget* expected_parent,
    const std::vector<GridTrack>& tracks,
    const std::vector<GridItem>& items,
    bool columns,
    float spacing
) {
    GridAxisLayout axis;
    axis.extents.reserve(tracks.size());
    axis.min_extents.reserve(tracks.size());
    axis.max_extents.reserve(tracks.size());
    axis.grow_weights.reserve(tracks.size());
    axis.shrink_weights.reserve(tracks.size());
    for (const GridTrack& track : tracks) {
        const float min_extent = track.min_extent;
        const float max_extent = std::max(min_extent, track_max_extent(track));
        axis.min_extents.push_back(min_extent);
        axis.max_extents.push_back(max_extent);
        axis.grow_weights.push_back(std::max(0.0f, track.grow));
        axis.shrink_weights.push_back(std::max(0.0f, track.shrink));
        axis.extents.push_back(clamp_float(track_base_extent(track), min_extent, max_extent));
    }

    for (const GridItem& item : items) {
        const size_t start = columns ? item.column : item.row;
        const size_t span = columns ? item.column_span : item.row_span;
        if (start >= tracks.size()) {
            continue;
        }
        tc_widget* child = resolve_child(
            document,
            expected_parent,
            item.handle,
            columns ? "GridLayout::measure(columns)" : "GridLayout::measure(rows)"
        );
        if (!child) {
            continue;
        }
        const tc_ui_size measured = measure_widget(child, document, unconstrained());
        apply_span_requirement(
            axis.extents,
            axis.max_extents,
            start,
            span,
            spacing,
            columns ? measured.width : measured.height
        );
    }
    return axis;
}

float axis_total_extent(const std::vector<float>& extents, float spacing) {
    if (extents.empty()) {
        return 0.0f;
    }
    float total = spacing * static_cast<float>(extents.size() - 1);
    for (float extent : extents) {
        total += extent;
    }
    return total;
}

} // namespace

const tc_widget_vtable NativeWidget::VTABLE {
    "NativeWidget",
    &NativeWidget::dispatch_measure,
    &NativeWidget::dispatch_layout,
    &NativeWidget::dispatch_paint,
    &NativeWidget::dispatch_pointer_event,
    &NativeWidget::dispatch_hit_test,
    &NativeWidget::dispatch_key_event,
    &NativeWidget::dispatch_text_event,
    &NativeWidget::dispatch_on_destroy,
};

NativeWidget::NativeWidget(const char* debug_name)
    : Widget(&VTABLE, debug_name) {}

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
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

BoxLayout& BoxLayout::set_spacing(float spacing) {
    spacing_ = std::max(0.0f, spacing);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

BoxLayout& BoxLayout::set_background(Color color) {
    background_ = color;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

BoxLayout& BoxLayout::set_border(Color color, float thickness) {
    border_ = color;
    border_thickness_ = std::max(0.0f, thickness);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
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

    if (!attach_child(c_widget(), handle, SIZE_MAX, "BoxLayout::add_child")) {
        return;
    }
    items_.erase(
        std::remove_if(
            items_.begin(),
            items_.end(),
            [handle](const LayoutItem& existing) { return tc_widget_handle_eq(existing.handle, handle); }
        ),
        items_.end()
    );

    LayoutItem item {};
    item.handle = handle;
    item.policy = policy;
    if (policy == LayoutPolicy::Fixed) {
        item.fixed_extent = std::max(0.0f, value);
        item.grow = 0.0f;
        item.shrink = 0.0f;
        item.min_extent = item.fixed_extent;
        item.max_extent = item.fixed_extent;
    } else if (policy == LayoutPolicy::Preferred) {
        item.grow = 0.0f;
        item.shrink = 0.0f;
    } else if (policy == LayoutPolicy::Flex) {
        item.flex = value > 0.0f ? value : 1.0f;
        item.grow = item.flex;
        item.shrink = item.flex;
    } else {
        item.grow = 1.0f;
        item.shrink = 1.0f;
    }
    items_.push_back(item);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
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

bool BoxLayout::set_child_extent_limits(tc_widget_handle handle, float min_extent, float max_extent) {
    for (LayoutItem& item : items_) {
        if (item.handle.index == handle.index && item.handle.generation == handle.generation) {
            item.min_extent = std::max(0.0f, min_extent);
            item.max_extent = std::max(0.0f, max_extent);
            if (item.max_extent > 0.0f && item.max_extent < item.min_extent) {
                item.max_extent = item.min_extent;
            }
            mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
            return true;
        }
    }
    return false;
}

std::vector<tc_widget_handle> BoxLayout::children() const {
    std::vector<tc_widget_handle> handles;
    handles.reserve(child_count());
    for (size_t index = 0; index < child_count(); ++index) {
        const tc_widget* child = child_at(index);
        if (child) {
            handles.push_back(child->handle);
        }
    }
    return handles;
}

tc_ui_size BoxLayout::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size content {0.0f, 0.0f};
    size_t live_children = 0;
    for (size_t index = 0; index < child_count(); ++index) {
        tc_widget* child = child_at(index);
        if (!child || !tc_widget_is_visible(child)) {
            continue;
        }
        const LayoutItem* stored_item = find_layout_item(items_, child->handle);
        const LayoutItem default_item {child->handle, LayoutPolicy::Stretch};
        const LayoutItem& item = stored_item ? *stored_item : default_item;
        tc_ui_size child_size = measure_widget(child, document, unconstrained());
        const float child_primary = item_basis(item, child_size, orientation_);
        if (orientation_ == Orientation::Vertical) {
            content.width = std::max(content.width, cross_size(child_size, orientation_));
            content.height += child_primary;
        } else {
            content.width += child_primary;
            content.height = std::max(content.height, cross_size(child_size, orientation_));
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
    content.width = std::max(content.width, min_size().width);
    content.height = std::max(content.height, min_size().height);
    return clamp_size(content, constraints);
}

void BoxLayout::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);

    struct LiveItem {
        tc_widget* widget = nullptr;
        float extent = 0.0f;
        float min_extent = 0.0f;
        float max_extent = kHuge;
        float grow = 0.0f;
        float shrink = 0.0f;
    };

    std::vector<LiveItem> live_items;
    live_items.reserve(child_count());
    float base_extent = 0.0f;
    for (size_t index = 0; index < child_count(); ++index) {
        tc_widget* child = child_at(index);
        if (!child || !tc_widget_is_visible(child)) {
            continue;
        }
        const LayoutItem* stored_item = find_layout_item(items_, child->handle);
        const LayoutItem default_item {child->handle, LayoutPolicy::Stretch};
        const LayoutItem& item = stored_item ? *stored_item : default_item;
        NativeWidget* native = native_widget_body(child);
        tc_ui_size measured = measure_widget(child, document, unconstrained());
        const float min_extent = item_min_extent(item, native, orientation_);
        const float max_extent = std::max(min_extent, item_max_extent(item, native, orientation_));
        LiveItem live {};
        live.widget = child;
        live.min_extent = min_extent;
        live.max_extent = max_extent;
        live.grow = std::max(0.0f, item.grow);
        live.shrink = std::max(0.0f, item.shrink);
        live.extent = clamp_float(item_basis(item, measured, orientation_), min_extent, max_extent);
        base_extent += live.extent;
        live_items.push_back(live);
    }
    if (live_items.empty()) {
        return;
    }

    tc_ui_rect content = inset_rect(rect, padding_);
    const float total_spacing = spacing_ * static_cast<float>(live_items.size() - 1);
    const float axis_extent = orientation_ == Orientation::Vertical ? content.height : content.width;
    const float available_extent = std::max(0.0f, axis_extent - total_spacing);
    std::vector<float> extents;
    std::vector<float> min_extents;
    std::vector<float> max_extents;
    std::vector<float> grow_weights;
    std::vector<float> shrink_weights;
    extents.reserve(live_items.size());
    min_extents.reserve(live_items.size());
    max_extents.reserve(live_items.size());
    grow_weights.reserve(live_items.size());
    shrink_weights.reserve(live_items.size());
    for (const LiveItem& live : live_items) {
        extents.push_back(live.extent);
        min_extents.push_back(live.min_extent);
        max_extents.push_back(live.max_extent);
        grow_weights.push_back(live.grow);
        shrink_weights.push_back(live.shrink);
    }

    if (available_extent > base_extent) {
        distribute_grow(extents, max_extents, grow_weights, available_extent - base_extent);
    } else if (available_extent < base_extent) {
        distribute_shrink(extents, min_extents, shrink_weights, base_extent - available_extent);
    }

    float cursor = orientation_ == Orientation::Vertical ? content.y : content.x;
    for (size_t i = 0; i < live_items.size(); ++i) {
        const LiveItem& live = live_items[i];
        tc_ui_rect child_rect {};
        if (orientation_ == Orientation::Vertical) {
            child_rect = tc_ui_rect {content.x, cursor, content.width, extents[i]};
        } else {
            child_rect = tc_ui_rect {cursor, content.y, extents[i], content.height};
        }
        layout_widget(live.widget, document, child_rect);
        cursor += extents[i] + spacing_;
    }
}

void BoxLayout::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    if (color_visible(background_)) {
        tc_ui_painter_fill_rect(context, bounds(), background_.c_color());
    }
    if (color_visible(border_) && border_thickness_ > 0.0f) {
        tc_ui_painter_stroke_rect(context, bounds(), border_.c_color(), border_thickness_);
    }

    tc_ui_painter_push_clip(context, bounds());
    for (size_t index = 0; index < child_count(); ++index) {
        paint_widget(child_at(index), document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result BoxLayout::pointer_event(
    tc_ui_document* document,
    const tc_ui_pointer_event* event
) {
    if (!event || !visible() || !enabled() || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    for (size_t index = child_count(); index > 0; --index) {
        tc_widget* child = child_at(index - 1);
        if (!child || !tc_widget_is_visible(child) || !tc_widget_is_enabled(child) ||
            !child->vtable || !child->vtable->pointer_event) {
            continue;
        }
        if (child->vtable->pointer_event(child, document, event) == TC_UI_EVENT_HANDLED) {
            return TC_UI_EVENT_HANDLED;
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle BoxLayout::hit_test(tc_ui_document* document, float x, float y) {
    if (!visible() || !rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    for (size_t index = child_count(); index > 0; --index) {
        tc_widget* child = child_at(index - 1);
        if (!child || !tc_widget_is_visible(child) || !child->vtable || !child->vtable->hit_test) {
            continue;
        }
        tc_widget_handle hit = child->vtable->hit_test(child, document, x, y);
        if (!tc_widget_handle_is_invalid(hit)) {
            return hit;
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

GridLayout::GridLayout(const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "GridLayout") {}

GridLayout& GridLayout::set_padding(EdgeInsets padding) {
    padding_ = padding;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GridLayout& GridLayout::set_spacing(float column_spacing, float row_spacing) {
    column_spacing_ = std::max(0.0f, column_spacing);
    row_spacing_ = std::max(0.0f, row_spacing);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GridLayout& GridLayout::set_background(Color color) {
    background_ = color;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GridLayout& GridLayout::set_border(Color color, float thickness) {
    border_ = color;
    border_thickness_ = std::max(0.0f, thickness);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

size_t GridLayout::add_column(LayoutPolicy policy, float value) {
    columns_.push_back(make_grid_track(policy, value));
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return columns_.size() - 1;
}

size_t GridLayout::add_row(LayoutPolicy policy, float value) {
    rows_.push_back(make_grid_track(policy, value));
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return rows_.size() - 1;
}

void GridLayout::add_child(
    tc_widget_handle handle,
    size_t row,
    size_t column,
    size_t row_span,
    size_t column_span
) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot add invalid child handle to GridLayout");
        return;
    }
    if (row_span == 0 || column_span == 0) {
        tc_log_error("[termin-gui-native] cannot add GridLayout child with zero span");
        return;
    }
    if (!attach_child(c_widget(), handle, SIZE_MAX, "GridLayout::add_child")) {
        return;
    }
    items_.erase(
        std::remove_if(
            items_.begin(),
            items_.end(),
            [handle](const GridItem& existing) { return tc_widget_handle_eq(existing.handle, handle); }
        ),
        items_.end()
    );
    while (rows_.size() < row + row_span) {
        add_row(LayoutPolicy::Stretch);
    }
    while (columns_.size() < column + column_span) {
        add_column(LayoutPolicy::Stretch);
    }
    items_.push_back(GridItem {handle, row, column, row_span, column_span});
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

bool GridLayout::set_column_extent_limits(size_t column, float min_extent, float max_extent) {
    if (column >= columns_.size()) {
        return false;
    }
    set_track_limits(columns_[column], min_extent, max_extent);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
}

bool GridLayout::set_row_extent_limits(size_t row, float min_extent, float max_extent) {
    if (row >= rows_.size()) {
        return false;
    }
    set_track_limits(rows_[row], min_extent, max_extent);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
}

tc_ui_size GridLayout::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    GridAxisLayout columns = build_grid_axis(document, c_widget(), columns_, items_, true, column_spacing_);
    GridAxisLayout rows = build_grid_axis(document, c_widget(), rows_, items_, false, row_spacing_);
    tc_ui_size measured {
        axis_total_extent(columns.extents, column_spacing_) + padding_.left + padding_.right,
        axis_total_extent(rows.extents, row_spacing_) + padding_.top + padding_.bottom
    };
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void GridLayout::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (columns_.empty() || rows_.empty()) {
        return;
    }

    tc_ui_rect content = inset_rect(rect, padding_);
    GridAxisLayout columns = build_grid_axis(document, c_widget(), columns_, items_, true, column_spacing_);
    GridAxisLayout rows = build_grid_axis(document, c_widget(), rows_, items_, false, row_spacing_);

    const float column_spacing_total = column_spacing_ * static_cast<float>(columns.extents.empty() ? 0 : columns.extents.size() - 1);
    const float row_spacing_total = row_spacing_ * static_cast<float>(rows.extents.empty() ? 0 : rows.extents.size() - 1);
    const float available_width = std::max(0.0f, content.width - column_spacing_total);
    const float available_height = std::max(0.0f, content.height - row_spacing_total);
    const float base_width = axis_total_extent(columns.extents, 0.0f);
    const float base_height = axis_total_extent(rows.extents, 0.0f);
    if (available_width > base_width) {
        distribute_grow(columns.extents, columns.max_extents, columns.grow_weights, available_width - base_width);
    } else if (available_width < base_width) {
        distribute_shrink(columns.extents, columns.min_extents, columns.shrink_weights, base_width - available_width);
    }
    if (available_height > base_height) {
        distribute_grow(rows.extents, rows.max_extents, rows.grow_weights, available_height - base_height);
    } else if (available_height < base_height) {
        distribute_shrink(rows.extents, rows.min_extents, rows.shrink_weights, base_height - available_height);
    }

    std::vector<float> column_offsets(columns.extents.size(), content.x);
    std::vector<float> row_offsets(rows.extents.size(), content.y);
    for (size_t i = 1; i < columns.extents.size(); ++i) {
        column_offsets[i] = column_offsets[i - 1] + columns.extents[i - 1] + column_spacing_;
    }
    for (size_t i = 1; i < rows.extents.size(); ++i) {
        row_offsets[i] = row_offsets[i - 1] + rows.extents[i - 1] + row_spacing_;
    }

    for (const GridItem& item : items_) {
        if (item.column >= columns.extents.size() || item.row >= rows.extents.size()) {
            continue;
        }
        tc_widget* child = resolve_child(document, c_widget(), item.handle, "GridLayout::layout");
        if (!child) {
            continue;
        }
        const size_t column_end = std::min(columns.extents.size(), item.column + item.column_span);
        const size_t row_end = std::min(rows.extents.size(), item.row + item.row_span);
        tc_ui_rect child_rect {
            column_offsets[item.column],
            row_offsets[item.row],
            0.0f,
            0.0f
        };
        for (size_t column = item.column; column < column_end; ++column) {
            child_rect.width += columns.extents[column];
        }
        for (size_t row = item.row; row < row_end; ++row) {
            child_rect.height += rows.extents[row];
        }
        child_rect.width += column_spacing_ * static_cast<float>(column_end - item.column - 1);
        child_rect.height += row_spacing_ * static_cast<float>(row_end - item.row - 1);
        layout_widget(child, document, child_rect);
    }
}

void GridLayout::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    if (color_visible(background_)) {
        tc_ui_painter_fill_rect(context, bounds(), background_.c_color());
    }
    if (color_visible(border_) && border_thickness_ > 0.0f) {
        tc_ui_painter_stroke_rect(context, bounds(), border_.c_color(), border_thickness_);
    }

    tc_ui_painter_push_clip(context, bounds());
    for (size_t index = 0; index < child_count(); ++index) {
        paint_widget(child_at(index), document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result GridLayout::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || !visible() || !enabled() || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    for (size_t index = child_count(); index > 0; --index) {
        tc_widget* child = child_at(index - 1);
        if (!child || !tc_widget_is_visible(child) || !tc_widget_is_enabled(child) ||
            !child->vtable || !child->vtable->pointer_event) {
            continue;
        }
        if (child->vtable->pointer_event(child, document, event) == TC_UI_EVENT_HANDLED) {
            return TC_UI_EVENT_HANDLED;
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle GridLayout::hit_test(tc_ui_document* document, float x, float y) {
    if (!visible() || !rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    for (size_t index = child_count(); index > 0; --index) {
        tc_widget* child = child_at(index - 1);
        if (!child || !tc_widget_is_visible(child) || !child->vtable || !child->vtable->hit_test) {
            continue;
        }
        tc_widget_handle hit = child->vtable->hit_test(child, document, x, y);
        if (!tc_widget_handle_is_invalid(hit)) {
            return hit;
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

GroupBox::GroupBox(std::string title, const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "GroupBox"),
      title_(std::move(title)) {
    set_preferred_size(tc_ui_size {240.0f, 140.0f});
}

GroupBox& GroupBox::set_title(std::string title) {
    title_ = std::move(title);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GroupBox& GroupBox::set_padding(EdgeInsets padding) {
    padding_ = padding;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GroupBox& GroupBox::set_background(Color color) {
    background_ = color;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GroupBox& GroupBox::set_border(Color color, float thickness) {
    border_ = color;
    border_thickness_ = std::max(0.0f, thickness);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
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
    tc_ui_size measured {
        std::max(preferred_size().width, static_cast<float>(title_.size()) * 8.0f + padding_.left + padding_.right),
        preferred_size().height
    };
    if (!tc_widget_handle_is_invalid(this->content())) {
        if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::measure")) {
            const tc_ui_size content_size = measure_widget(content, document, unconstrained());
            measured.width = std::max(measured.width, content_size.width + padding_.left + padding_.right);
            measured.height = std::max(
                measured.height,
                content_size.height + header_height_ + padding_.top + padding_.bottom
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
    layout_widget(content, document, content_rect());
}

void GroupBox::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    if (color_visible(background_)) {
        tc_ui_painter_fill_rect(context, bounds(), background_.c_color());
    }
    if (color_visible(border_) && border_thickness_ > 0.0f) {
        tc_ui_painter_stroke_rect(context, bounds(), border_.c_color(), border_thickness_);
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {bounds().x, bounds().y + header_height_},
            tc_ui_point {bounds().x + bounds().width, bounds().y + header_height_},
            border_.c_color(),
            border_thickness_
        );
    }
    if (!title_.empty()) {
        tc_ui_rect title_clip {
            bounds().x + padding_.left,
            bounds().y,
            std::max(0.0f, bounds().width - padding_.left - padding_.right),
            header_height_
        };
        tc_ui_painter_push_clip(context, title_clip);
        tc_ui_painter_draw_text(
            context,
            title_.c_str(),
            tc_ui_point {bounds().x + padding_.left, bounds().y + 20.0f},
            13.0f,
            tc_ui_color {0.86f, 0.90f, 0.96f, 1.0f}
        );
        tc_ui_painter_pop_clip(context);
    }

    const tc_ui_rect clip = content_rect();
    tc_ui_painter_push_clip(context, clip);
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::paint");
        paint_widget(content, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result GroupBox::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || !visible() || !enabled() || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (!tc_widget_handle_is_invalid(this->content()) && rect_contains(content_rect(), event->x, event->y)) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::pointer_event");
        if (content && tc_widget_is_enabled(content) && content->vtable && content->vtable->pointer_event &&
            content->vtable->pointer_event(content, document, event) == TC_UI_EVENT_HANDLED) {
            return TC_UI_EVENT_HANDLED;
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle GroupBox::hit_test(tc_ui_document* document, float x, float y) {
    if (!visible() || !rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(this->content()) && rect_contains(content_rect(), x, y)) {
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

tc_ui_rect GroupBox::content_rect() const {
    tc_ui_rect rect {
        bounds().x + padding_.left,
        bounds().y + header_height_ + padding_.top,
        std::max(0.0f, bounds().width - padding_.left - padding_.right),
        std::max(0.0f, bounds().height - header_height_ - padding_.top - padding_.bottom)
    };
    return rect;
}

Splitter::Splitter(Orientation orientation, const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "Splitter"),
      orientation_(orientation) {
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {320.0f, 180.0f}
        : tc_ui_size {240.0f, 260.0f});
}

void Splitter::set_first(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid Splitter first handle");
        return;
    }
    const tc_widget_handle previous = this->first();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (tc_widget_handle_eq(this->second(), handle)) {
        tc_log_error("[termin-gui-native] Splitter first and second widgets must be distinct");
        return;
    }
    if (!attach_child(c_widget(), handle, 0, "Splitter::set_first")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void Splitter::set_second(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid Splitter second handle");
        return;
    }
    const tc_widget_handle previous = this->second();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (tc_widget_handle_eq(this->first(), handle)) {
        tc_log_error("[termin-gui-native] Splitter first and second widgets must be distinct");
        return;
    }
    if (!attach_child(c_widget(), handle, 1, "Splitter::set_second")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

Splitter& Splitter::set_split_fraction(float fraction) {
    split_fraction_ = clamp_float(fraction, 0.05f, 0.95f);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    return *this;
}

Splitter& Splitter::set_min_extents(float first_min, float second_min) {
    first_min_extent_ = std::max(0.0f, first_min);
    second_min_extent_ = std::max(0.0f, second_min);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Splitter& Splitter::set_divider_thickness(float thickness) {
    divider_thickness_ = std::max(1.0f, thickness);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

tc_ui_size Splitter::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size first_size {0.0f, 0.0f};
    tc_ui_size second_size {0.0f, 0.0f};
    if (!tc_widget_handle_is_invalid(this->first())) {
        if (tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::measure(first)")) {
            first_size = measure_widget(first, document, unconstrained());
        }
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        if (tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::measure(second)")) {
            second_size = measure_widget(second, document, unconstrained());
        }
    }

    tc_ui_size measured {};
    if (orientation_ == Orientation::Horizontal) {
        measured.width = first_size.width + second_size.width + divider_thickness_;
        measured.height = std::max(first_size.height, second_size.height);
    } else {
        measured.width = std::max(first_size.width, second_size.width);
        measured.height = first_size.height + second_size.height + divider_thickness_;
    }
    measured.width = std::max({measured.width, preferred_size().width, min_size().width});
    measured.height = std::max({measured.height, preferred_size().height, min_size().height});
    return clamp_size(measured, constraints);
}

void Splitter::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    layout_children(document);
}

void Splitter::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    if (!tc_widget_handle_is_invalid(this->first())) {
        tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::paint(first)");
        paint_widget(first, document, context);
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::paint(second)");
        paint_widget(second, document, context);
    }
    tc_ui_painter_pop_clip(context);
    tc_ui_painter_fill_rect(context, divider_rect(), divider_color_.c_color());
}

tc_ui_event_result Splitter::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (!captured && !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(divider_rect(), event->x, event->y)) {
        tc_ui_document_set_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && captured) {
        const float axis = split_axis_extent();
        if (axis > 0.0f) {
            const float position = orientation_ == Orientation::Horizontal
                ? event->x - bounds().x
                : event->y - bounds().y;
            set_split_fraction(position / axis);
            layout_children(document);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && captured) {
        tc_ui_document_release_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }

    auto dispatch_child = [this, document, event](tc_widget_handle handle) {
        tc_widget* child = resolve_child(document, c_widget(), handle, "Splitter::pointer_event");
        if (!child || !tc_widget_is_enabled(child) || !child->vtable || !child->vtable->pointer_event) {
            return TC_UI_EVENT_IGNORED;
        }
        return child->vtable->pointer_event(child, document, event);
    };
    if (dispatch_child(this->second()) == TC_UI_EVENT_HANDLED) {
        return TC_UI_EVENT_HANDLED;
    }
    return dispatch_child(this->first());
}

tc_widget_handle Splitter::hit_test(tc_ui_document* document, float x, float y) {
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (rect_contains(divider_rect(), x, y)) {
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }
    auto hit_child = [this, document, x, y](tc_widget_handle handle) {
        tc_widget* child = resolve_child(document, c_widget(), handle, "Splitter::hit_test");
        if (!child || !child->vtable || !child->vtable->hit_test) {
            return tc_widget_handle_invalid();
        }
        return child->vtable->hit_test(child, document, x, y);
    };
    tc_widget_handle second_hit = hit_child(this->second());
    if (!tc_widget_handle_is_invalid(second_hit)) {
        return second_hit;
    }
    tc_widget_handle first_hit = hit_child(this->first());
    if (!tc_widget_handle_is_invalid(first_hit)) {
        return first_hit;
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

tc_ui_rect Splitter::divider_rect() const {
    const float axis = split_axis_extent();
    if (orientation_ == Orientation::Horizontal) {
        const float first_extent = clamp_float(
            axis * split_fraction_,
            first_min_extent_,
            std::max(first_min_extent_, axis - second_min_extent_)
        );
        return tc_ui_rect {bounds().x + first_extent, bounds().y, divider_thickness_, bounds().height};
    }
    const float first_extent = clamp_float(
        axis * split_fraction_,
        first_min_extent_,
        std::max(first_min_extent_, axis - second_min_extent_)
    );
    return tc_ui_rect {bounds().x, bounds().y + first_extent, bounds().width, divider_thickness_};
}

void Splitter::layout_children(tc_ui_document* document) {
    const float axis = split_axis_extent();
    const float first_extent = clamp_float(
        axis * split_fraction_,
        first_min_extent_,
        std::max(first_min_extent_, axis - second_min_extent_)
    );
    const float second_extent = std::max(0.0f, axis - first_extent);
    if (!tc_widget_handle_is_invalid(this->first())) {
        tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::layout(first)");
        tc_ui_rect first_rect = orientation_ == Orientation::Horizontal
            ? tc_ui_rect {bounds().x, bounds().y, first_extent, bounds().height}
            : tc_ui_rect {bounds().x, bounds().y, bounds().width, first_extent};
        layout_widget(first, document, first_rect);
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::layout(second)");
        tc_ui_rect second_rect = orientation_ == Orientation::Horizontal
            ? tc_ui_rect {
                bounds().x + first_extent + divider_thickness_,
                bounds().y,
                second_extent,
                bounds().height
            }
            : tc_ui_rect {
                bounds().x,
                bounds().y + first_extent + divider_thickness_,
                bounds().width,
                second_extent
            };
        layout_widget(second, document, second_rect);
    }
}

float Splitter::split_axis_extent() const {
    const float axis = orientation_ == Orientation::Horizontal ? bounds().width : bounds().height;
    return std::max(0.0f, axis - divider_thickness_);
}

ScrollArea::ScrollArea(const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "ScrollArea") {
    set_preferred_size(tc_ui_size {240.0f, 180.0f});
}

void ScrollArea::set_content(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid ScrollArea content handle");
        return;
    }
    const tc_widget_handle previous = this->content();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (!attach_child(c_widget(), handle, 0, "ScrollArea::set_content")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ScrollArea::set_scroll(float x, float y) {
    scroll_x_ = std::max(0.0f, x);
    scroll_y_ = std::max(0.0f, y);
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size ScrollArea::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size measured = preferred_size();
    if (!tc_widget_handle_is_invalid(this->content())) {
        if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::measure")) {
            tc_ui_size content_size = measure_widget(content, document, unconstrained());
            measured.width = std::max(measured.width, std::min(content_size.width, preferred_size().width));
            measured.height = std::max(measured.height, std::min(content_size.height, preferred_size().height));
        }
    }
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void ScrollArea::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    content_size_ = tc_ui_size {0.0f, 0.0f};
    if (tc_widget_handle_is_invalid(this->content())) {
        return;
    }
    tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::layout");
    if (!content) {
        return;
    }

    tc_ui_size measured = measure_widget(content, document, unconstrained());
    content_size_ = tc_ui_size {
        std::max(measured.width, rect.width),
        std::max(measured.height, rect.height)
    };
    clamp_scroll();
    layout_widget(
        content,
        document,
        tc_ui_rect {rect.x - scroll_x_, rect.y - scroll_y_, content_size_.width, content_size_.height}
    );
}

void ScrollArea::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::paint");
        paint_widget(content, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result ScrollArea::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_WHEEL) {
        const float delta_y = event->wheel_y != 0.0f ? -event->wheel_y * wheel_step_ : 0.0f;
        const float delta_x = event->wheel_x != 0.0f ? -event->wheel_x * wheel_step_ : 0.0f;
        set_scroll(scroll_x_ + delta_x, scroll_y_ + delta_y);
        if (!tc_widget_handle_is_invalid(this->content())) {
            if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::wheel")) {
                layout_widget(
                    content,
                    document,
                    tc_ui_rect {
                        bounds().x - scroll_x_,
                        bounds().y - scroll_y_,
                        content_size_.width,
                        content_size_.height
                    }
                );
            }
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (tc_widget_handle_is_invalid(this->content())) {
        return TC_UI_EVENT_IGNORED;
    }
    tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::pointer_event");
    if (!content || !tc_widget_is_enabled(content) || !content->vtable || !content->vtable->pointer_event) {
        return TC_UI_EVENT_IGNORED;
    }
    return content->vtable->pointer_event(content, document, event);
}

tc_widget_handle ScrollArea::hit_test(tc_ui_document* document, float x, float y) {
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::hit_test");
        if (content && content->vtable && content->vtable->hit_test) {
            tc_widget_handle hit = content->vtable->hit_test(content, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

void ScrollArea::clamp_scroll() {
    scroll_x_ = clamp_float(scroll_x_, 0.0f, std::max(0.0f, content_size_.width - bounds().width));
    scroll_y_ = clamp_float(scroll_y_, 0.0f, std::max(0.0f, content_size_.height - bounds().height));
}

TabView::TabView(const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "TabView") {
    set_preferred_size(tc_ui_size {320.0f, 220.0f});
}

void TabView::add_page(std::string title, tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot add invalid TabView page handle");
        return;
    }
    const auto duplicate = std::find_if(
        pages_.begin(),
        pages_.end(),
        [handle](const TabPage& page) { return tc_widget_handle_eq(page.handle, handle); }
    );
    if (duplicate != pages_.end()) {
        tc_log_error("[termin-gui-native] cannot add the same widget to TabView twice");
        return;
    }
    if (!attach_child(c_widget(), handle, SIZE_MAX, "TabView::add_page")) {
        return;
    }
    pages_.push_back(TabPage {std::move(title), handle});
    if (child_count() == 1) {
        selected_index_ = 0;
    }
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TabView::set_selected_index(size_t index) {
    if (index >= child_count() || selected_index_ == index) {
        return;
    }
    selected_index_ = index;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size TabView::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size content {preferred_size().width, std::max(0.0f, preferred_size().height - header_height_)};
    for (size_t index = 0; index < child_count(); ++index) {
        tc_widget* child = child_at(index);
        if (!child || !tc_widget_is_visible(child)) {
            continue;
        }
        tc_ui_size child_size = measure_widget(child, document, unconstrained());
        content.width = std::max(content.width, child_size.width);
        content.height = std::max(content.height, child_size.height);
    }
    return clamp_size(tc_ui_size {content.width, content.height + header_height_}, constraints);
}

void TabView::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (child_count() == 0 || selected_index_ >= child_count()) {
        return;
    }
    tc_widget* selected = child_at(selected_index_);
    layout_widget(selected, document, page_rect());
}

void TabView::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds(), tc_ui_color {0.10f, 0.11f, 0.13f, 1.0f});
    const float tab_width = child_count() == 0
        ? min_tab_width_
        : std::max(min_tab_width_, bounds().width / static_cast<float>(child_count()));
    for (size_t i = 0; i < child_count(); ++i) {
        const tc_widget* child = child_at(i);
        const TabPage* page = child ? find_tab_page(pages_, child->handle) : nullptr;
        tc_ui_rect tab {
            bounds().x + tab_width * static_cast<float>(i),
            bounds().y,
            tab_width,
            header_height_
        };
        const bool selected = i == selected_index_;
        tc_ui_painter_fill_rect(
            context,
            tab,
            selected ? tc_ui_color {0.20f, 0.26f, 0.34f, 1.0f} : tc_ui_color {0.13f, 0.14f, 0.16f, 1.0f}
        );
        tc_ui_painter_stroke_rect(context, tab, tc_ui_color {0.34f, 0.36f, 0.40f, 1.0f}, 1.0f);
        tc_ui_painter_push_clip(context, tab);
        tc_ui_painter_draw_text(
            context,
            page ? page->title.c_str() : "",
            tc_ui_point {tab.x + 8.0f, tab.y + 21.0f},
            13.0f,
            tc_ui_color {0.88f, 0.91f, 0.96f, 1.0f}
        );
        tc_ui_painter_pop_clip(context);
    }

    tc_ui_rect body = page_rect();
    tc_ui_painter_push_clip(context, body);
    if (child_count() > 0 && selected_index_ < child_count()) {
        tc_widget* selected = child_at(selected_index_);
        paint_widget(selected, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result TabView::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN && event->y < bounds().y + header_height_ && child_count() > 0) {
        const float tab_width = std::max(min_tab_width_, bounds().width / static_cast<float>(child_count()));
        const size_t index = static_cast<size_t>(std::max(0.0f, (event->x - bounds().x) / tab_width));
        if (index < child_count()) {
            set_selected_index(index);
            if (tc_widget* selected = child_at(selected_index_)) {
                layout_widget(selected, document, page_rect());
            }
            return TC_UI_EVENT_HANDLED;
        }
    }
    if (child_count() == 0 || selected_index_ >= child_count() || !rect_contains(page_rect(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    tc_widget* selected = child_at(selected_index_);
    if (!selected || !tc_widget_is_enabled(selected) || !selected->vtable || !selected->vtable->pointer_event) {
        return TC_UI_EVENT_IGNORED;
    }
    return selected->vtable->pointer_event(selected, document, event);
}

tc_widget_handle TabView::hit_test(tc_ui_document* document, float x, float y) {
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (y < bounds().y + header_height_) {
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }
    if (child_count() > 0 && selected_index_ < child_count() && rect_contains(page_rect(), x, y)) {
        tc_widget* selected = child_at(selected_index_);
        if (selected && selected->vtable && selected->vtable->hit_test) {
            tc_widget_handle hit = selected->vtable->hit_test(selected, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

tc_ui_rect TabView::page_rect() const {
    return tc_ui_rect {
        bounds().x,
        bounds().y + header_height_,
        bounds().width,
        std::max(0.0f, bounds().height - header_height_)
    };
}

Panel::Panel(const char* debug_name)
    : NativeWidget(debug_name) {
    set_preferred_size(tc_ui_size {96.0f, 64.0f});
}

Panel& Panel::set_fill(Color color) {
    fill_ = color;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Panel& Panel::set_border(Color color, float thickness) {
    border_ = color;
    border_thickness_ = std::max(0.0f, thickness);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

void Panel::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds(), fill_.c_color());
    if (border_thickness_ > 0.0f && color_visible(border_)) {
        tc_ui_painter_stroke_rect(context, bounds(), border_.c_color(), border_thickness_);
    }
}

Button::Button(std::string text, Color fill)
    : NativeWidget("Button"), text_(std::move(text)), fill_(fill) {
    set_preferred_size(tc_ui_size {96.0f, 36.0f});
}

Button::Button(Color fill)
    : Button(std::string {}, fill) {}

Button& Button::set_accent(Color color) {
    accent_ = color;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Button& Button::set_text(std::string text) {
    text_ = std::move(text);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

void Button::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds(), fill_.c_color());
    tc_ui_painter_stroke_rect(context, bounds(), accent_.c_color(), 2.0f);
    const float y = bounds().y + bounds().height * 0.5f;
    tc_ui_painter_draw_line(
        context,
        tc_ui_point {bounds().x + 14.0f, y},
        tc_ui_point {bounds().x + bounds().width - 14.0f, y},
        accent_.c_color(),
        2.0f
    );
    if (!text_.empty()) {
        tc_ui_painter_push_clip(context, bounds());
        tc_ui_painter_draw_text(
            context,
            text_.c_str(),
            tc_ui_point {bounds().x + 12.0f, bounds().y + bounds().height * 0.5f + 5.0f},
            14.0f,
            tc_ui_color {0.94f, 0.97f, 1.0f, 1.0f}
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
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Label& Label::set_color(Color color) {
    color_ = color;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Label& Label::set_font_size(float font_size) {
    font_size_ = std::max(1.0f, font_size);
    update_min_size();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

void Label::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    tc_ui_painter_draw_text(
        context,
        text_.c_str(),
        tc_ui_point {bounds().x, bounds().y + font_size_},
        font_size_,
        color_.c_color()
    );
    tc_ui_painter_pop_clip(context);
}

void Label::update_min_size() {
    set_preferred_size(tc_ui_size {
        std::max(1.0f, static_cast<float>(text_.size()) * font_size_ * 0.56f),
        font_size_ * 1.35f
    });
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

Checkbox::Checkbox(bool checked)
    : NativeWidget("Checkbox"), checked_(checked) {
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

void Checkbox::paint(tc_ui_document*, tc_ui_paint_context* context) {
    const float side = std::min(bounds().width, bounds().height);
    const tc_ui_rect box {
        bounds().x + (bounds().width - side) * 0.5f,
        bounds().y + (bounds().height - side) * 0.5f,
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

void ProgressBar::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds(), tc_ui_color {0.09f, 0.10f, 0.12f, 1.0f});
    tc_ui_rect fill = bounds();
    fill.width *= value_;
    tc_ui_painter_fill_rect(context, fill, tc_ui_color {0.25f, 0.58f, 0.88f, 1.0f});
    tc_ui_painter_stroke_rect(context, bounds(), tc_ui_color {0.38f, 0.42f, 0.48f, 1.0f}, 1.0f);
}

Separator::Separator(Orientation orientation)
    : NativeWidget("Separator"), orientation_(orientation) {
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {24.0f, thickness_}
        : tc_ui_size {thickness_, 24.0f});
}

Separator& Separator::set_color(Color color) {
    color_ = color;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Separator& Separator::set_thickness(float thickness) {
    thickness_ = std::max(1.0f, thickness);
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {preferred_size().width, thickness_}
        : tc_ui_size {thickness_, preferred_size().height});
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

void Separator::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_rect rect = bounds();
    if (orientation_ == Orientation::Horizontal) {
        rect.y += std::max(0.0f, (rect.height - thickness_) * 0.5f);
        rect.height = std::min(rect.height, thickness_);
    } else {
        rect.x += std::max(0.0f, (rect.width - thickness_) * 0.5f);
        rect.width = std::min(rect.width, thickness_);
    }
    tc_ui_painter_fill_rect(context, rect, color_.c_color());
}

TextInput::TextInput(std::string text)
    : NativeWidget("TextInput"), text_(std::move(text)), caret_(text_.size()) {
    set_focusable(true);
    update_preferred_size();
}

void TextInput::set_text(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    caret_ = std::min(caret_, text_.size());
    update_preferred_size();
    emit_changed();
}

void TextInput::set_caret(size_t caret) {
    const size_t next = std::min(caret, text_.size());
    if (caret_ == next) {
        return;
    }
    caret_ = next;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void TextInput::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const bool focused = tc_widget_handle_eq(tc_ui_document_focused_widget(document), handle());
    const tc_ui_color border = focused
        ? tc_ui_color {0.38f, 0.62f, 0.92f, 1.0f}
        : tc_ui_color {0.36f, 0.38f, 0.42f, 1.0f};
    tc_ui_painter_fill_rect(context, bounds(), tc_ui_color {0.08f, 0.09f, 0.11f, 1.0f});
    tc_ui_painter_stroke_rect(context, bounds(), border, focused ? 2.0f : 1.0f);
    const tc_ui_rect text_clip {
        bounds().x + 8.0f,
        bounds().y + 2.0f,
        std::max(0.0f, bounds().width - 16.0f),
        std::max(0.0f, bounds().height - 4.0f)
    };
    tc_ui_painter_push_clip(context, text_clip);
    tc_ui_painter_draw_text(
        context,
        text_.c_str(),
        tc_ui_point {bounds().x + 8.0f, bounds().y + bounds().height * 0.5f + font_size_ * 0.35f},
        font_size_,
        text_color_.c_color()
    );
    if (focused) {
        const float glyph_width = font_size_ * 0.56f;
        const float caret_x = bounds().x + 8.0f + glyph_width * static_cast<float>(caret_);
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {caret_x, bounds().y + 7.0f},
            tc_ui_point {caret_x, bounds().y + bounds().height - 7.0f},
            tc_ui_color {0.86f, 0.92f, 1.0f, 1.0f},
            1.0f
        );
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result TextInput::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || event->type != TC_UI_POINTER_DOWN || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    tc_ui_document_set_focus(document, handle());
    const float glyph_width = std::max(1.0f, font_size_ * 0.56f);
    const float local_x = std::max(0.0f, event->x - bounds().x - 8.0f);
    set_caret(static_cast<size_t>(std::floor(local_x / glyph_width + 0.5f)));
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result TextInput::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN) {
        return TC_UI_EVENT_IGNORED;
    }
    switch (event->key) {
    case TC_UI_KEY_BACKSPACE:
        if (caret_ > 0) {
            text_.erase(caret_ - 1, 1);
            caret_ -= 1;
            update_preferred_size();
            emit_changed();
        }
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_DELETE:
        if (caret_ < text_.size()) {
            text_.erase(caret_, 1);
            update_preferred_size();
            emit_changed();
        }
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_LEFT:
        set_caret(caret_ > 0 ? caret_ - 1 : 0);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_RIGHT:
        set_caret(caret_ + 1);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_HOME:
        set_caret(0);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_END:
        set_caret(text_.size());
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_ENTER:
        submitted_.emit(*this, text_);
        return TC_UI_EVENT_HANDLED;
    default:
        return TC_UI_EVENT_IGNORED;
    }
}

tc_ui_event_result TextInput::text_event(tc_ui_document*, const tc_ui_text_event* event) {
    if (!event || !event->text || event->text[0] == '\0') {
        return TC_UI_EVENT_IGNORED;
    }
    text_.insert(caret_, event->text);
    caret_ += std::strlen(event->text);
    update_preferred_size();
    emit_changed();
    return TC_UI_EVENT_HANDLED;
}

void TextInput::update_preferred_size() {
    set_preferred_size(tc_ui_size {
        std::max(160.0f, static_cast<float>(text_.size()) * font_size_ * 0.56f + 18.0f),
        34.0f
    });
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TextInput::emit_changed() {
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, text_);
}

Slider::Slider(float value)
    : NativeWidget("Slider") {
    set_preferred_size(tc_ui_size {140.0f, 28.0f});
    set_value(value);
}

void Slider::set_value(float value) {
    const float next = clamp_float(value, 0.0f, 1.0f);
    if (std::fabs(next - value_) <= 0.0001f) {
        return;
    }
    value_ = next;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, value_);
}

void Slider::paint(tc_ui_document*, tc_ui_paint_context* context) {
    const float center_y = bounds().y + bounds().height * 0.5f;
    const float left = bounds().x + 10.0f;
    const float right = bounds().x + bounds().width - 10.0f;
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

tc_ui_event_result Slider::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        dragging_ = true;
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    } else if (event->type == TC_UI_POINTER_DOWN) {
        return TC_UI_EVENT_IGNORED;
    } else if (event->type == TC_UI_POINTER_MOVE && !(dragging_ || captured)) {
        return TC_UI_EVENT_IGNORED;
    } else if (event->type == TC_UI_POINTER_UP && (dragging_ || captured)) {
        dragging_ = false;
        if (captured) {
            tc_ui_document_release_pointer_capture(document, handle());
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    } else if (event->type != TC_UI_POINTER_DOWN && event->type != TC_UI_POINTER_MOVE) {
        return TC_UI_EVENT_IGNORED;
    }
    const float left = bounds().x + 10.0f;
    const float right = bounds().x + bounds().width - 10.0f;
    if (right <= left) {
        return TC_UI_EVENT_HANDLED;
    }
    set_value((event->x - left) / (right - left));
    return TC_UI_EVENT_HANDLED;
}

Swatch::Swatch(Color color)
    : NativeWidget("Swatch"), color_(color) {
    set_preferred_size(tc_ui_size {36.0f, 36.0f});
}

void Swatch::paint(tc_ui_document*, tc_ui_paint_context* context) {
    tc_ui_painter_fill_rect(context, bounds(), color_.c_color());
    tc_ui_painter_stroke_rect(context, bounds(), tc_ui_color {0.88f, 0.90f, 0.94f, 1.0f}, 1.0f);
}

Spacer::Spacer(tc_ui_size size)
    : NativeWidget("Spacer") {
    set_preferred_size(size);
}

} // namespace termin::gui_native
