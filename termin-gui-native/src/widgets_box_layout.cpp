#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
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
    tc_ui_document*,
    const tc_ui_pointer_event* event
) {
    (void)event;
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

} // namespace termin::gui_native
