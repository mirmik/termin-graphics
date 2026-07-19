#include <termin/gui_native/overlay_layout.hpp>

#include "widgets_internal.hpp"

#include <algorithm>

namespace termin::gui_native {
using namespace detail;

namespace {

bool same_handle(tc_widget_handle lhs, tc_widget_handle rhs) {
    return tc_widget_handle_eq(lhs, rhs);
}

} // namespace

OverlayLayout::OverlayLayout(const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "OverlayLayout") {
    set_mouse_transparent(true);
}

bool OverlayLayout::add_child(
    tc_widget_handle handle,
    OverlayAnchor anchor,
    tc_ui_point offset) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot add invalid child to OverlayLayout");
        return false;
    }
    if (!attach_child(c_widget(), handle, SIZE_MAX, "OverlayLayout::add_child")) {
        return false;
    }
    placements_.erase(
        std::remove_if(
            placements_.begin(),
            placements_.end(),
            [handle](const OverlayPlacement& item) {
                return same_handle(item.handle, handle);
            }),
        placements_.end());
    placements_.push_back({handle, anchor, offset});
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
}

bool OverlayLayout::remove_child(tc_widget_handle handle) {
    tc_widget* child = nullptr;
    for (size_t index = 0; index < child_count(); ++index) {
        tc_widget* candidate = child_at(index);
        if (candidate && same_handle(candidate->handle, handle)) {
            child = candidate;
            break;
        }
    }
    if (!child || !tc_widget_remove_child(c_widget(), child)) {
        return false;
    }
    placements_.erase(
        std::remove_if(
            placements_.begin(),
            placements_.end(),
            [handle](const OverlayPlacement& item) {
                return same_handle(item.handle, handle);
            }),
        placements_.end());
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return true;
}

bool OverlayLayout::set_placement(
    tc_widget_handle handle,
    OverlayAnchor anchor,
    tc_ui_point offset) {
    for (OverlayPlacement& item : placements_) {
        if (same_handle(item.handle, handle)) {
            item.anchor = anchor;
            item.offset = offset;
            mark_dirty(TC_WIDGET_DIRTY_LAYOUT);
            return true;
        }
    }
    tc_log_error("[termin-gui-native] cannot place unknown OverlayLayout child");
    return false;
}

const OverlayPlacement* OverlayLayout::placement(tc_widget_handle handle) const {
    for (const OverlayPlacement& item : placements_) {
        if (same_handle(item.handle, handle)) {
            return &item;
        }
    }
    return nullptr;
}

tc_ui_size OverlayLayout::measure(
    tc_ui_document* document,
    tc_ui_constraints constraints) {
    tc_ui_size measured = NativeWidget::measure(document, constraints);
    for (size_t index = 0; index < child_count(); ++index) {
        tc_widget* child = child_at(index);
        if (!child || !tc_widget_is_visible(child)) {
            continue;
        }
        const tc_ui_size child_size = measure_widget(child, document, constraints);
        measured.width = std::max(measured.width, child_size.width);
        measured.height = std::max(measured.height, child_size.height);
    }
    return clamp_size(measured, constraints);
}

void OverlayLayout::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    for (size_t index = 0; index < child_count(); ++index) {
        tc_widget* child = child_at(index);
        if (!child || !tc_widget_is_visible(child)) {
            continue;
        }
        const OverlayPlacement* stored = placement(child->handle);
        const OverlayPlacement fallback{child->handle, OverlayAnchor::Fill, {}};
        const OverlayPlacement& item = stored ? *stored : fallback;
        if (item.anchor == OverlayAnchor::Fill) {
            layout_widget(child, document, rect);
            continue;
        }

        const tc_ui_size size = measure_widget(child, document, unconstrained());
        float x = rect.x + item.offset.x;
        float y = rect.y + item.offset.y;
        if (item.anchor == OverlayAnchor::TopRight ||
            item.anchor == OverlayAnchor::BottomRight) {
            x = rect.x + rect.width - size.width + item.offset.x;
        }
        if (item.anchor == OverlayAnchor::BottomLeft ||
            item.anchor == OverlayAnchor::BottomRight) {
            y = rect.y + rect.height - size.height + item.offset.y;
        }
        layout_widget(child, document, {x, y, size.width, size.height});
    }
}

void OverlayLayout::paint(
    tc_ui_document* document,
    tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    const tc_ui_style_override local_style = style_override();
    if ((local_style.fields & TC_UI_STYLE_BACKGROUND) != 0 &&
        local_style.value.background.a > 0.0f) {
        tc_ui_painter_fill_rect(context, bounds(), local_style.value.background);
    }
    for (size_t index = 0; index < child_count(); ++index) {
        paint_widget(child_at(index), document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_widget_handle OverlayLayout::hit_test(
    tc_ui_document* document,
    float x,
    float y) {
    if (!visible() || !rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    for (size_t index = child_count(); index > 0; --index) {
        tc_widget* child = child_at(index - 1);
        if (!child || !tc_widget_is_visible(child) || !child->vtable ||
            !child->vtable->hit_test) {
            continue;
        }
        const tc_widget_handle hit =
            child->vtable->hit_test(child, document, x, y);
        if (!tc_widget_handle_is_invalid(hit)) {
            return hit;
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

} // namespace termin::gui_native
