#include "widgets_internal.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

TableWidget::TableWidget(std::shared_ptr<TableModel> model,
                         std::shared_ptr<TableColumnModel> columns)
    : NativeWidget("TableWidget"),
      model_(model ? std::move(model) : std::make_shared<TableModel>()),
      columns_(columns ? std::move(columns) : std::make_shared<TableColumnModel>()) {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_focusable(true);
    set_preferred_size(tc_ui_size{400.0f, 220.0f});
    observed_model_revision_ = model_->revision();
    observed_column_revision_ = columns_->revision();
    connect_models();
}

TableWidget::~TableWidget() { disconnect_models(); }

void TableWidget::connect_models() {
    if (model_ && model_connection_ == 0) {
        model_connection_ = model_->changed().connect(
            [this](TableModel&, const TableChange& change) { on_rows_changed(change); });
    }
    if (columns_ && column_connection_ == 0) {
        column_connection_ =
            columns_->changed().connect([this](TableColumnModel&, const TableColumnChange& change) {
                on_columns_changed(change);
            });
    }
}

void TableWidget::disconnect_models() {
    if (model_ && model_connection_ != 0)
        model_->changed().disconnect(model_connection_);
    if (columns_ && column_connection_ != 0)
        columns_->changed().disconnect(column_connection_);
    model_connection_ = 0;
    column_connection_ = 0;
}

void TableWidget::set_model(std::shared_ptr<TableModel> model) {
    disconnect_models();
    const bool had_selection = selection_.clear();
    model_ = model ? std::move(model) : std::make_shared<TableModel>();
    observed_model_revision_ = model_->revision();
    hovered_ = SelectionModel::npos;
    scroll_y_ = 0.0f;
    connect_models();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (had_selection)
        emit_selection_changed();
}

void TableWidget::set_column_model(std::shared_ptr<TableColumnModel> columns) {
    disconnect_models();
    columns_ = columns ? std::move(columns) : std::make_shared<TableColumnModel>();
    observed_column_revision_ = columns_->revision();
    resizing_column_ = SelectionModel::npos;
    connect_models();
    compute_column_layout();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void TableWidget::on_rows_changed(const TableChange& change) {
    observed_model_revision_ = model_->revision();
    bool selection_changed = false;
    switch (change.kind) {
    case TableChangeKind::Reset:
        selection_changed = selection_.clear();
        break;
    case TableChangeKind::Insert:
        selection_changed = selection_.items_inserted(change.index, change.count);
        break;
    case TableChangeKind::Erase:
        selection_changed = selection_.items_erased(change.index, change.count, model_->size());
        break;
    case TableChangeKind::Update:
        break;
    }
    if (hovered_ >= model_->size())
        hovered_ = SelectionModel::npos;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (selection_changed)
        emit_selection_changed();
}

void TableWidget::on_columns_changed(const TableColumnChange&) {
    observed_column_revision_ = columns_->revision();
    if (resizing_column_ >= columns_->size())
        resizing_column_ = SelectionModel::npos;
    compute_column_layout();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void TableWidget::sync_models() {
    if (model_->revision() != observed_model_revision_) {
        observed_model_revision_ = model_->revision();
        const bool changed = selection_.reconcile(model_->size());
        if (hovered_ >= model_->size())
            hovered_ = SelectionModel::npos;
        clamp_scroll();
        if (changed)
            emit_selection_changed();
    }
    if (columns_->revision() != observed_column_revision_) {
        observed_column_revision_ = columns_->revision();
        compute_column_layout();
    }
}

void TableWidget::set_selection_mode(SelectionMode mode) {
    const std::vector<size_t> before = selection_.selected_indices();
    selection_.set_mode(mode);
    if (before != selection_.selected_indices())
        emit_selection_changed();
}

void TableWidget::set_row_height(float height) {
    if (!std::isfinite(height) || height <= 0.0f) {
        tc_log_error("[termin-gui-native] TableWidget rejected invalid row height");
        throw std::invalid_argument("row height must be finite and positive");
    }
    row_height_ = height;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TableWidget::set_header_height(float height) {
    if (!std::isfinite(height) || height <= 0.0f) {
        tc_log_error("[termin-gui-native] TableWidget rejected invalid header height");
        throw std::invalid_argument("header height must be finite and positive");
    }
    header_height_ = height;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

float TableWidget::body_height() const { return std::max(0.0f, bounds().height - header_height_); }

float TableWidget::content_height() const {
    return static_cast<float>(model_->size()) * row_height_;
}

void TableWidget::clamp_scroll() {
    scroll_y_ = clamp_float(scroll_y_, 0.0f, std::max(0.0f, content_height() - body_height()));
}

void TableWidget::set_scroll_y(float offset) {
    if (!std::isfinite(offset)) {
        tc_log_error("[termin-gui-native] TableWidget rejected non-finite scroll offset");
        throw std::invalid_argument("scroll offset must be finite");
    }
    const float before = scroll_y_;
    scroll_y_ = offset;
    clamp_scroll();
    if (before != scroll_y_)
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

std::pair<size_t, size_t> TableWidget::visible_range() const {
    if (model_->empty() || body_height() <= 0.0f)
        return {0, 0};
    const size_t first = std::min(
        model_->size(), static_cast<size_t>(std::max(0.0f, std::floor(scroll_y_ / row_height_))));
    const size_t last =
        std::min(model_->size(),
                 static_cast<size_t>(std::ceil((scroll_y_ + body_height()) / row_height_)) + 1);
    return {first, last};
}

void TableWidget::ensure_visible(size_t index) {
    if (index >= model_->size())
        return;
    const float top = static_cast<float>(index) * row_height_;
    const float bottom = top + row_height_;
    if (top < scroll_y_)
        set_scroll_y(top);
    else if (bottom > scroll_y_ + body_height())
        set_scroll_y(bottom - body_height());
}

bool TableWidget::select_row(size_t index, bool toggle, bool extend, bool additive) {
    sync_models();
    if (index >= model_->size() || !model_->row_at(index).data.enabled)
        return false;
    bool changed = false;
    if (extend)
        changed = selection_.extend_to(index, additive);
    else if (toggle)
        changed = selection_.toggle(index);
    else
        changed = selection_.select_only(index);
    ensure_visible(index);
    if (changed)
        emit_selection_changed();
    return changed;
}

bool TableWidget::clear_selection() {
    const bool changed = selection_.clear();
    if (changed)
        emit_selection_changed();
    return changed;
}

void TableWidget::compute_column_layout() {
    column_layout_.assign(columns_->size(), TableColumnLayout{});
    if (columns_->empty())
        return;

    float fixed_total = 0.0f;
    float stretch_min_total = 0.0f;
    float stretch_weight = 0.0f;
    for (size_t index = 0; index < columns_->size(); ++index) {
        const TableColumn& column = columns_->column(index);
        if (column.policy == TableColumnPolicy::Fixed) {
            float width = std::max(column.min_width, column.width);
            if (column.max_width > 0.0f)
                width = std::min(column.max_width, width);
            column_layout_[index].width = width;
            fixed_total += width;
        } else {
            column_layout_[index].width = column.min_width;
            stretch_min_total += column.min_width;
            stretch_weight += column.stretch;
        }
    }

    float extra = std::max(0.0f, bounds().width - fixed_total - stretch_min_total);
    std::vector<bool> active(columns_->size(), true);
    while (extra > 0.001f && stretch_weight > 0.0f) {
        float consumed = 0.0f;
        float next_weight = 0.0f;
        for (size_t index = 0; index < columns_->size(); ++index) {
            const TableColumn& column = columns_->column(index);
            if (column.policy != TableColumnPolicy::Stretch || !active[index])
                continue;
            const float share = extra * column.stretch / stretch_weight;
            float width = column_layout_[index].width + share;
            if (column.max_width > 0.0f && width >= column.max_width) {
                width = column.max_width;
                active[index] = false;
            } else {
                next_weight += column.stretch;
            }
            consumed += width - column_layout_[index].width;
            column_layout_[index].width = width;
        }
        if (consumed <= 0.001f)
            break;
        extra = std::max(0.0f, extra - consumed);
        stretch_weight = next_weight;
    }

    float x = 0.0f;
    for (TableColumnLayout& column : column_layout_) {
        column.x = x;
        x += column.width;
    }
}

tc_ui_size TableWidget::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    sync_models();
    const tc_ui_style style = computed_style(document);
    return clamp_size(tc_ui_size{std::max(preferred_size().width, style.min_width),
                                 std::max(preferred_size().height, style.min_height)},
                      constraints);
}

void TableWidget::layout(tc_ui_document* document, tc_ui_rect rect) {
    sync_models();
    NativeWidget::layout(document, rect);
    compute_column_layout();
    clamp_scroll();
}

void TableWidget::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    sync_models();
    const tc_ui_style style = computed_style(document);
    const tc_ui_rect header{bounds().x, bounds().y, bounds().width, header_height_};
    tc_ui_color header_color = style.accent;
    header_color.a *= 0.20f;
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_push_clip(context, bounds());
    tc_ui_painter_fill_rect(context, header, header_color);
    for (size_t column_index = 0; column_index < columns_->size(); ++column_index) {
        const TableColumn& column = columns_->column(column_index);
        const TableColumnLayout layout = column_layout_[column_index];
        const tc_ui_rect cell{bounds().x + layout.x, bounds().y, layout.width, header_height_};
        tc_ui_painter_push_clip(context, cell);
        tc_ui_painter_draw_text(context, column.header.c_str(),
                                tc_ui_point{cell.x + cell_padding_, cell.y + cell.height * 0.66f},
                                std::max(9.0f, style.font_size - 1.0f), style.foreground);
        tc_ui_painter_pop_clip(context);
        if (column_index + 1 < columns_->size()) {
            const float separator = cell.x + cell.width;
            tc_ui_painter_draw_line(context, tc_ui_point{separator, cell.y + 2.0f},
                                    tc_ui_point{separator, cell.y + cell.height - 2.0f},
                                    style.border, style.border_width);
        }
    }
    tc_ui_painter_draw_line(context, tc_ui_point{header.x, header.y + header.height},
                            tc_ui_point{header.x + header.width, header.y + header.height},
                            style.border, style.border_width);

    const tc_ui_rect body{bounds().x, bounds().y + header_height_, bounds().width, body_height()};
    tc_ui_painter_push_clip(context, body);
    if (model_->empty()) {
        tc_ui_color muted = style.foreground;
        muted.a *= 0.6f;
        tc_ui_painter_draw_text(context, "No data",
                                tc_ui_point{body.x + cell_padding_, body.y + row_height_ * 0.66f},
                                style.font_size, muted);
    } else {
        const auto [first, last] = visible_range();
        for (size_t row_index = first; row_index < last; ++row_index) {
            const TableRow& row = model_->row_at(row_index);
            const float y = body.y + static_cast<float>(row_index) * row_height_ - scroll_y_;
            const tc_ui_rect row_rect{body.x, y, body.width, row_height_};
            tc_ui_color row_color = row_index % 2 == 0 ? style.background : style.border;
            row_color.a = row_index % 2 == 0 ? 0.18f : 0.10f;
            if (selection_.contains(row_index) || hovered_ == row_index) {
                row_color = style.accent;
                row_color.a *= selection_.contains(row_index) ? 0.42f : 0.20f;
            }
            tc_ui_painter_fill_rect(context, row_rect, row_color);
            tc_ui_color foreground = style.foreground;
            if (!row.data.enabled)
                foreground.a *= 0.45f;
            const size_t cell_count = std::min(row.data.cells.size(), column_layout_.size());
            for (size_t column_index = 0; column_index < cell_count; ++column_index) {
                const TableColumnLayout layout = column_layout_[column_index];
                const tc_ui_rect cell{row_rect.x + layout.x, row_rect.y, layout.width,
                                      row_rect.height};
                tc_ui_painter_push_clip(context, cell);
                tc_ui_painter_draw_text(
                    context, row.data.cells[column_index].c_str(),
                    tc_ui_point{cell.x + cell_padding_, cell.y + cell.height * 0.66f},
                    style.font_size, foreground);
                tc_ui_painter_pop_clip(context);
            }
        }
    }
    tc_ui_painter_pop_clip(context);
    tc_ui_painter_pop_clip(context);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
}

size_t TableWidget::row_index_at(float x, float y) const {
    if (!rect_contains(bounds(), x, y) || y < bounds().y + header_height_)
        return SIZE_MAX;
    const float local = y - bounds().y - header_height_ + scroll_y_;
    if (local < 0.0f)
        return SIZE_MAX;
    const size_t index = static_cast<size_t>(local / row_height_);
    return index < model_->size() ? index : SIZE_MAX;
}

size_t TableWidget::column_index_at(float x) const {
    const float local = x - bounds().x;
    for (size_t index = 0; index < column_layout_.size(); ++index) {
        const TableColumnLayout& column = column_layout_[index];
        if (local >= column.x && local < column.x + column.width)
            return index;
    }
    return SIZE_MAX;
}

size_t TableWidget::resize_border_at(float x) const {
    const float local = x - bounds().x;
    for (size_t index = 0; index < column_layout_.size(); ++index) {
        if (!columns_->column(index).resizable)
            continue;
        const float border = column_layout_[index].x + column_layout_[index].width;
        if (std::abs(local - border) <= resize_zone_)
            return index;
    }
    return SIZE_MAX;
}

bool TableWidget::apply_selection(size_t index, int32_t modifiers) {
    if (index >= model_->size() || !model_->row_at(index).data.enabled)
        return false;
    if ((modifiers & TC_UI_MOD_SHIFT) != 0) {
        select_row(index, false, true, (modifiers & TC_UI_MOD_CTRL) != 0);
    } else {
        select_row(index, (modifiers & TC_UI_MOD_CTRL) != 0, false, false);
    }
    return true;
}

tc_ui_event_result TableWidget::pointer_event(tc_ui_document* document,
                                              const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    sync_models();
    if (resizing_column_ != SelectionModel::npos) {
        if (event->type == TC_UI_POINTER_MOVE) {
            const float width = columns_->resize(resizing_column_,
                                                 resize_start_width_ + event->x - resize_start_x_);
            column_resized_.emit(*this, resizing_column_, width);
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_UP) {
            const size_t column = resizing_column_;
            resizing_column_ = SelectionModel::npos;
            tc_ui_document_release_pointer_capture(document, handle());
            (void)column;
            return TC_UI_EVENT_HANDLED;
        }
    }
    if (event->type == TC_UI_POINTER_LEAVE) {
        if (hovered_ != SelectionModel::npos) {
            hovered_ = SelectionModel::npos;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return TC_UI_EVENT_IGNORED;
    }
    if (!rect_contains(bounds(), event->x, event->y))
        return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_WHEEL) {
        const float before = scroll_y_;
        set_scroll_y(scroll_y_ - event->wheel_y * row_height_ * wheel_rows_);
        return before != scroll_y_ ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_MOVE) {
        const size_t next = row_index_at(event->x, event->y);
        if (next != hovered_) {
            hovered_ = next;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type != TC_UI_POINTER_DOWN)
        return TC_UI_EVENT_IGNORED;
    tc_ui_document_set_focus(document, handle());
    if (event->y < bounds().y + header_height_) {
        const size_t border = resize_border_at(event->x);
        if (border != SIZE_MAX) {
            resizing_column_ = border;
            resize_start_x_ = event->x;
            resize_start_width_ = column_layout_[border].width;
            tc_ui_document_set_pointer_capture(document, handle());
            return TC_UI_EVENT_HANDLED;
        }
        const size_t column = column_index_at(event->x);
        if (column != SIZE_MAX) {
            header_clicked_.emit(*this, column, columns_->column(column));
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }
    const size_t row = row_index_at(event->x, event->y);
    if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::RIGHT)) {
        if (row != SIZE_MAX && model_->row_at(row).data.enabled)
            apply_selection(row, event->modifiers);
        context_menu_requested_.emit(*this, row == SIZE_MAX ? -1 : static_cast<int64_t>(row),
                                     event->x, event->y);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->button != tcbase::mouse_button_value(tcbase::MouseButton::LEFT))
        return TC_UI_EVENT_IGNORED;
    if (row == SIZE_MAX || !model_->row_at(row).data.enabled) {
        activation_clicks_.clear();
        return TC_UI_EVENT_IGNORED;
    }
    const bool selected = apply_selection(row, event->modifiers);
    const TableRow& item = model_->row_at(row);
    if (activation_clicks_.press(item.data.stable_id, event->click_count)) {
        activated_.emit(*this, row, item.id, item.data);
        return TC_UI_EVENT_HANDLED;
    }
    return selected ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
}

size_t TableWidget::next_enabled(size_t from, int direction) const {
    if (model_->empty())
        return SelectionModel::npos;
    std::ptrdiff_t index =
        from == SelectionModel::npos
            ? (direction > 0 ? 0 : static_cast<std::ptrdiff_t>(model_->size()) - 1)
            : static_cast<std::ptrdiff_t>(from) + direction;
    while (index >= 0 && index < static_cast<std::ptrdiff_t>(model_->size())) {
        if (model_->row_at(static_cast<size_t>(index)).data.enabled) {
            return static_cast<size_t>(index);
        }
        index += direction;
    }
    return SelectionModel::npos;
}

tc_ui_event_result TableWidget::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN)
        return TC_UI_EVENT_IGNORED;
    sync_models();
    if (event->key == TC_UI_KEY_ENTER) {
        const size_t current = selection_.current();
        if (current < model_->size() && model_->row_at(current).data.enabled) {
            const TableRow& row = model_->row_at(current);
            activated_.emit(*this, current, row.id, row.data);
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }
    size_t target = SelectionModel::npos;
    if (event->key == TC_UI_KEY_HOME)
        target = next_enabled(SelectionModel::npos, 1);
    else if (event->key == TC_UI_KEY_END)
        target = next_enabled(SelectionModel::npos, -1);
    else if (event->key == TC_UI_KEY_UP_ARROW)
        target = next_enabled(selection_.current(), -1);
    else if (event->key == TC_UI_KEY_DOWN_ARROW)
        target = next_enabled(selection_.current(), 1);
    else
        return TC_UI_EVENT_IGNORED;
    if (target != SelectionModel::npos)
        apply_selection(target, event->modifiers);
    return TC_UI_EVENT_HANDLED;
}

void TableWidget::emit_selection_changed() {
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    selection_changed_.emit(*this, selection_.selected_indices());
}

} // namespace termin::gui_native
