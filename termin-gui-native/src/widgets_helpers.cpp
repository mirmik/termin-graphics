#include "widgets_internal.hpp"

#include <tcbase/tc_log.h>

namespace termin::gui_native::detail {

const float kHuge = 1000000.0f;

float clamp_float(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

bool color_visible(Color color) {
    return color.a > 0.0f;
}

bool color_visible(tc_ui_color color) {
    return color.a > 0.0f;
}

void set_style_color(Widget& widget, tc_ui_style_field_mask field, tc_ui_color color) {
    tc_ui_style_override style_override = widget.style_override();
    style_override.fields |= field;
    if (field == TC_UI_STYLE_BACKGROUND) style_override.value.background = color;
    else if (field == TC_UI_STYLE_FOREGROUND) style_override.value.foreground = color;
    else if (field == TC_UI_STYLE_BORDER) style_override.value.border = color;
    else if (field == TC_UI_STYLE_ACCENT) style_override.value.accent = color;
    else throw std::invalid_argument("unsupported native UI color style field");
    if (!widget.set_style_override(style_override)) {
        throw std::runtime_error("failed to set native UI color style override");
    }
}

void set_style_metric(Widget& widget, tc_ui_style_field_mask field, float value) {
    tc_ui_style_override style_override = widget.style_override();
    style_override.fields |= field;
    if (field == TC_UI_STYLE_BORDER_WIDTH) style_override.value.border_width = value;
    else if (field == TC_UI_STYLE_CORNER_RADIUS) style_override.value.corner_radius = value;
    else if (field == TC_UI_STYLE_FONT_SIZE) style_override.value.font_size = value;
    else throw std::invalid_argument("unsupported native UI metric style field");
    if (!widget.set_style_override(style_override)) {
        throw std::runtime_error("failed to set native UI metric style override");
    }
}

bool rect_contains(tc_ui_rect rect, float x, float y) {
    return x >= rect.x && y >= rect.y &&
           x <= rect.x + rect.width &&
           y <= rect.y + rect.height;
}

bool command_modifier(int32_t modifiers) {
    return (modifiers & (TC_UI_MOD_CTRL | TC_UI_MOD_SUPER)) != 0;
}

bool key_matches_ascii(int32_t key, char lower) {
    return key == static_cast<int32_t>(lower) ||
        key == static_cast<int32_t>(lower - ('a' - 'A'));
}

bool decode_utf8(std::string_view text, size_t& offset, uint32_t& codepoint) {
    if (offset >= text.size()) {
        return false;
    }
    const auto byte = [&text](size_t index) {
        return static_cast<uint8_t>(text[index]);
    };
    const uint8_t first = byte(offset);
    size_t length = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if (first < 0x80u) {
        length = 1;
        value = first;
    } else if ((first & 0xe0u) == 0xc0u) {
        length = 2;
        value = first & 0x1fu;
        minimum = 0x80u;
    } else if ((first & 0xf0u) == 0xe0u) {
        length = 3;
        value = first & 0x0fu;
        minimum = 0x800u;
    } else if ((first & 0xf8u) == 0xf0u) {
        length = 4;
        value = first & 0x07u;
        minimum = 0x10000u;
    } else {
        return false;
    }
    if (offset + length > text.size()) {
        return false;
    }
    for (size_t index = 1; index < length; ++index) {
        const uint8_t continuation = byte(offset + index);
        if ((continuation & 0xc0u) != 0x80u) {
            return false;
        }
        value = (value << 6u) | (continuation & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) {
        return false;
    }
    offset += length;
    codepoint = value;
    return true;
}

bool valid_utf8(std::string_view text) {
    size_t offset = 0;
    while (offset < text.size()) {
        uint32_t codepoint = 0;
        if (!decode_utf8(text, offset, codepoint)) {
            return false;
        }
    }
    return true;
}

size_t utf8_floor_boundary(std::string_view text, size_t offset) {
    offset = std::min(offset, text.size());
    while (offset > 0 && offset < text.size() &&
           (static_cast<uint8_t>(text[offset]) & 0xc0u) == 0x80u) {
        --offset;
    }
    return offset;
}

size_t utf8_previous_boundary(std::string_view text, size_t offset) {
    offset = utf8_floor_boundary(text, offset);
    if (offset == 0) {
        return 0;
    }
    --offset;
    while (offset > 0 && (static_cast<uint8_t>(text[offset]) & 0xc0u) == 0x80u) {
        --offset;
    }
    return offset;
}

size_t utf8_next_boundary(std::string_view text, size_t offset) {
    offset = utf8_floor_boundary(text, offset);
    if (offset >= text.size()) {
        return text.size();
    }
    size_t next = offset;
    uint32_t codepoint = 0;
    if (!decode_utf8(text, next, codepoint)) {
        return std::min(offset + 1, text.size());
    }
    return next;
}

bool measure_text(
    tc_ui_document_handle document,
    std::string_view text,
    float font_size,
    tc_ui_text_metrics& metrics
) {
    return tc_ui_document_measure_text(
        document,
        text.data(),
        text.size(),
        font_size,
        &metrics
    );
}

float centered_text_baseline(
    tc_ui_document_handle document,
    std::string_view text,
    float font_size,
    tc_ui_rect rect
) {
    tc_ui_text_metrics metrics {};
    if (!measure_text(document, text, font_size, metrics)) {
        return rect.y + rect.height * 0.5f + font_size * 0.35f;
    }
    const float line_height = metrics.line_height > 0.0f ? metrics.line_height : metrics.height;
    return rect.y + std::max(0.0f, (rect.height - line_height) * 0.5f) + metrics.ascent;
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
    tc_ui_document_handle document,
    const tc_widget* expected_parent,
    tc_widget_handle handle,
    const char* owner
) {
    (void)owner;
    if (tc_ui_document_handle_is_invalid(document) || !expected_parent) {
        return nullptr;
    }
    for (size_t index = 0; index < expected_parent->child_count; ++index) {
        tc_widget* child = expected_parent->children[index];
        if (child && tc_widget_handle_eq(child->handle, handle) &&
            tc_ui_document_handle_eq(child->document, document) && tc_widget_is_visible(child)) {
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
    if (!parent || tc_ui_document_handle_is_invalid(parent->document)) {
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
    if (!parent || tc_ui_document_handle_is_invalid(parent->document) || tc_widget_handle_is_invalid(child_handle)) {
        return;
    }
    tc_widget* child = tc_ui_document_resolve_widget(parent->document, child_handle);
    if (child && child->parent == parent) {
        tc_widget_remove_child(parent, child);
    }
}

tc_ui_size measure_widget(tc_widget* widget, tc_ui_document_handle document, tc_ui_constraints constraints) {
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

void layout_widget(tc_widget* widget, tc_ui_document_handle document, tc_ui_rect rect) {
    if (widget && tc_widget_is_visible(widget) && widget->vtable && widget->vtable->layout) {
        widget->vtable->layout(widget, document, rect);
    }
}

void paint_widget(tc_widget* widget, tc_ui_document_handle document, tc_ui_paint_context* context) {
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


GridAxisLayout build_grid_axis(
    tc_ui_document_handle document,
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

} // namespace termin::gui_native::detail
