#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
namespace {

void allocate_grid_axis(
    GridAxisLayout& axis,
    float available_extent
) {
    const float base_extent = axis_total_extent(axis.extents, 0.0f);
    if (available_extent > base_extent) {
        distribute_grow(
            axis.extents, axis.max_extents, axis.grow_weights,
            available_extent - base_extent);
    } else if (available_extent < base_extent) {
        distribute_shrink(
            axis.extents, axis.min_extents, axis.shrink_weights,
            base_extent - available_extent);
    }
}

}  // namespace

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
    return add_column(make_grid_track(policy, value));
}

size_t GridLayout::add_column(GridTrack track) {
    columns_.push_back(track);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return columns_.size() - 1;
}

size_t GridLayout::add_row(LayoutPolicy policy, float value) {
    return add_row(make_grid_track(policy, value));
}

size_t GridLayout::add_row(GridTrack track) {
    rows_.push_back(track);
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

tc_ui_size GridLayout::measure(tc_ui_document_handle document, tc_ui_constraints constraints) {
    const tc_ui_size padding_extent{
        padding_.left + padding_.right,
        padding_.top + padding_.bottom};
    const tc_ui_size parent_extent{
        std::max(0.0f, constraints.max_size.width - padding_extent.width),
        std::max(0.0f, constraints.max_size.height - padding_extent.height)};
    const bool width_definite =
        constraints.max_size.width > 0.0f &&
        constraints.max_size.width < kHuge;
    const bool height_definite =
        constraints.max_size.height > 0.0f &&
        constraints.max_size.height < kHuge;
    GridAxisLayout columns = build_grid_axis(
        document, c_widget(), columns_, items_, true, column_spacing_,
        parent_extent, width_definite, height_definite);
    const float column_spacing_total =
        column_spacing_ *
        static_cast<float>(
            columns.extents.empty() ? 0 : columns.extents.size() - 1);
    if (width_definite) {
        allocate_grid_axis(
            columns,
            std::max(0.0f, parent_extent.width - column_spacing_total));
    }
    GridAxisLayout rows = build_grid_axis(
        document, c_widget(), rows_, items_, false, row_spacing_,
        parent_extent, width_definite, height_definite, &columns.extents,
        column_spacing_);
    const float row_spacing_total =
        row_spacing_ *
        static_cast<float>(
            rows.extents.empty() ? 0 : rows.extents.size() - 1);
    if (height_definite) {
        allocate_grid_axis(
            rows,
            std::max(0.0f, parent_extent.height - row_spacing_total));
    }
    tc_ui_size measured {
        axis_total_extent(columns.extents, column_spacing_) + padding_extent.width,
        axis_total_extent(rows.extents, row_spacing_) + padding_extent.height
    };
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void GridLayout::layout(tc_ui_document_handle document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (columns_.empty() || rows_.empty()) {
        return;
    }

    tc_ui_rect content = inset_rect(rect, padding_);
    const tc_ui_size parent_extent{content.width, content.height};
    GridAxisLayout columns = build_grid_axis(
        document, c_widget(), columns_, items_, true, column_spacing_,
        parent_extent, true, true);

    const float column_spacing_total = column_spacing_ * static_cast<float>(columns.extents.empty() ? 0 : columns.extents.size() - 1);
    const float available_width = std::max(0.0f, content.width - column_spacing_total);
    allocate_grid_axis(columns, available_width);
    GridAxisLayout rows = build_grid_axis(
        document, c_widget(), rows_, items_, false, row_spacing_,
        parent_extent, true, true, &columns.extents, column_spacing_);
    const float row_spacing_total = row_spacing_ * static_cast<float>(rows.extents.empty() ? 0 : rows.extents.size() - 1);
    const float available_height = std::max(0.0f, content.height - row_spacing_total);
    allocate_grid_axis(rows, available_height);

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
        const tc_ui_insets margin = tc_widget_layout_spec(child).margin;
        child_rect.x += margin.left;
        child_rect.y += margin.top;
        child_rect.width = std::max(
            0.0f, child_rect.width - margin.left - margin.right);
        child_rect.height = std::max(
            0.0f, child_rect.height - margin.top - margin.bottom);
        tc_ui_constraints child_constraints{
            tc_ui_size{0.0f, 0.0f},
            tc_ui_size{child_rect.width, child_rect.height}};
        const tc_ui_widget_layout_spec spec = tc_widget_layout_spec(child);
        if (spec.width.mode == TC_UI_LENGTH_AUTO ||
            spec.width.mode == TC_UI_LENGTH_FILL) {
            child_constraints.min_size.width = child_rect.width;
        }
        if (spec.height.mode == TC_UI_LENGTH_AUTO ||
            spec.height.mode == TC_UI_LENGTH_FILL) {
            child_constraints.min_size.height = child_rect.height;
        }
        const tc_ui_size child_size = measure_widget(
            child, document, child_constraints, parent_extent, true, true);
        child_rect.width = child_size.width;
        child_rect.height = child_size.height;
        layout_widget(child, document, child_rect);
    }
}

void GridLayout::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
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

tc_ui_event_result GridLayout::pointer_event(tc_ui_document_handle, const tc_ui_pointer_event*) {
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle GridLayout::hit_test(tc_ui_document_handle document, float x, float y) {
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
