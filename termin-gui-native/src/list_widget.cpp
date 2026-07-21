#include "widgets_internal.hpp"
#include <cmath>
#include <limits>

namespace termin::gui_native {
using namespace detail;

ListWidget::ListWidget(std::shared_ptr<CollectionModel> model)
    : NativeWidget("ListWidget"),
      model_(model ? std::move(model) : std::make_shared<CollectionModel>()) {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_focusable(true);
    set_preferred_size(tc_ui_size {260.0f, 180.0f});
    observed_revision_ = model_->revision();
    connect_model();
}

ListWidget::~ListWidget() {
    disconnect_model();
}

void ListWidget::set_model(std::shared_ptr<CollectionModel> model) {
    disconnect_model();
    const bool had_selection = selection_.clear();
    model_ = model ? std::move(model) : std::make_shared<CollectionModel>();
    observed_revision_ = 0;
    scroll_y_ = 0.0f;
    hovered_ = SelectionModel::npos;
    sync_model();
    connect_model();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (had_selection) emit_selection_changed();
}

void ListWidget::connect_model() {
    if (!model_ || model_connection_ != 0) return;
    model_connection_ = model_->changed().connect(
        [this](CollectionModel&, const CollectionChange& change) { on_model_changed(change); }
    );
}

void ListWidget::disconnect_model() {
    if (model_ && model_connection_ != 0) model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void ListWidget::on_model_changed(const CollectionChange& change) {
    observed_revision_ = model_->revision();
    bool selection_changed = false;
    switch (change.kind) {
    case CollectionChangeKind::Reset:
        selection_changed = selection_.clear();
        break;
    case CollectionChangeKind::Insert:
        selection_changed = selection_.items_inserted(change.index, change.count);
        break;
    case CollectionChangeKind::Erase:
        selection_changed = selection_.items_erased(change.index, change.count, model_->size());
        break;
    case CollectionChangeKind::Update:
        break;
    }
    if (hovered_ >= model_->size()) hovered_ = SelectionModel::npos;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (selection_changed) emit_selection_changed();
}

void ListWidget::set_selection_mode(SelectionMode mode) {
    const std::vector<size_t> before = selection_.selected_indices();
    selection_.set_mode(mode);
    if (before != selection_.selected_indices()) emit_selection_changed();
}

void ListWidget::set_row_height(float height) {
    if (!std::isfinite(height) || height <= 0.0f) {
        tc_log_error("[termin-gui-native] ListWidget rejected invalid row height");
        throw std::invalid_argument("row height must be finite and positive");
    }
    row_height_ = height;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ListWidget::set_row_spacing(float spacing) {
    if (!std::isfinite(spacing) || spacing < 0.0f) {
        tc_log_error("[termin-gui-native] ListWidget rejected invalid row spacing");
        throw std::invalid_argument("row spacing must be finite and non-negative");
    }
    row_spacing_ = spacing;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

float ListWidget::content_height() const {
    if (!model_ || model_->empty()) return 0.0f;
    return static_cast<float>(model_->size()) * row_height_
        + static_cast<float>(model_->size() - 1) * row_spacing_;
}

void ListWidget::clamp_scroll() {
    scroll_y_ = clamp_float(scroll_y_, 0.0f, std::max(0.0f, content_height() - bounds().height));
}

void ListWidget::set_scroll_y(float offset) {
    if (!std::isfinite(offset)) {
        tc_log_error("[termin-gui-native] ListWidget rejected non-finite scroll offset");
        throw std::invalid_argument("scroll offset must be finite");
    }
    const float before = scroll_y_;
    scroll_y_ = offset;
    clamp_scroll();
    if (scroll_y_ != before) mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

std::pair<size_t, size_t> ListWidget::visible_range() const {
    if (!model_ || model_->empty() || bounds().height <= 0.0f) return {0, 0};
    const float stride = row_height_ + row_spacing_;
    const size_t first = std::min(
        model_->size(),
        static_cast<size_t>(std::max(0.0f, std::floor(scroll_y_ / stride)))
    );
    const size_t last = std::min(
        model_->size(),
        static_cast<size_t>(std::ceil((scroll_y_ + bounds().height) / stride)) + 1
    );
    return {first, last};
}

void ListWidget::ensure_visible(size_t index) {
    if (!model_ || index >= model_->size()) return;
    const float stride = row_height_ + row_spacing_;
    const float top = static_cast<float>(index) * stride;
    const float bottom = top + row_height_;
    if (top < scroll_y_) set_scroll_y(top);
    else if (bottom > scroll_y_ + bounds().height) set_scroll_y(bottom - bounds().height);
}

bool ListWidget::select_index(size_t index, bool toggle, bool extend, bool additive) {
    sync_model();
    if (!model_ || index >= model_->size() || !model_->item(index).enabled) return false;
    bool changed = false;
    if (extend) changed = selection_.extend_to(index, additive);
    else if (toggle) changed = selection_.toggle(index);
    else changed = selection_.select_only(index);
    ensure_visible(index);
    if (changed) emit_selection_changed();
    return changed;
}

bool ListWidget::clear_selection() {
    const bool changed = selection_.clear();
    if (changed) emit_selection_changed();
    return changed;
}

void ListWidget::sync_model() {
    if (!model_) return;
    if (model_->revision() == observed_revision_) return;
    observed_revision_ = model_->revision();
    const bool selection_changed = selection_.reconcile(model_->size());
    if (hovered_ >= model_->size()) hovered_ = SelectionModel::npos;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (selection_changed) emit_selection_changed();
}

tc_ui_size ListWidget::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    sync_model();
    const tc_ui_style style = computed_style(document);
    return clamp_size(
        tc_ui_size {
            std::max(preferred_size().width, style.min_width),
            std::max(preferred_size().height, style.min_height)
        },
        constraints
    );
}

void ListWidget::layout(tc_ui_document* document, tc_ui_rect rect) {
    sync_model();
    NativeWidget::layout(document, rect);
    clamp_scroll();
}

void ListWidget::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    sync_model();
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_push_clip(context, bounds());
    if (model_->empty()) {
        tc_ui_color muted = style.foreground;
        muted.a *= 0.6f;
        tc_ui_painter_draw_text(
            context, "No items",
            tc_ui_point {bounds().x + item_padding_, bounds().y + row_height_ * 0.68f},
            style.font_size, muted
        );
    } else {
        const auto [first, last] = visible_range();
        const float stride = row_height_ + row_spacing_;
        for (size_t index = first; index < last; ++index) {
            const CollectionItem& item = model_->item(index);
            const float y = bounds().y + static_cast<float>(index) * stride - scroll_y_;
            const tc_ui_rect row {bounds().x, y, bounds().width, row_height_};
            if (selection_.contains(index) || hovered_ == index) {
                tc_ui_color highlight = style.accent;
                highlight.a *= selection_.contains(index) ? 0.42f : 0.20f;
                tc_ui_painter_fill_rounded_rect(context, row, std::min(4.0f, row_height_ * 0.2f), highlight);
            }
            tc_ui_color foreground = style.foreground;
            if (!item.enabled) foreground.a *= 0.45f;
            if (item.subtitle.empty()) {
                tc_ui_painter_draw_text(
                    context, item.text.c_str(),
                    tc_ui_point {row.x + item_padding_, row.y + row.height * 0.66f},
                    style.font_size, foreground
                );
            } else {
                tc_ui_painter_draw_text(
                    context, item.text.c_str(),
                    tc_ui_point {row.x + item_padding_, row.y + style.font_size + 3.0f},
                    style.font_size, foreground
                );
                tc_ui_color subtitle = foreground;
                subtitle.a *= 0.65f;
                tc_ui_painter_draw_text(
                    context, item.subtitle.c_str(),
                    tc_ui_point {row.x + item_padding_, row.y + row.height - 5.0f},
                    std::max(9.0f, style.font_size - 2.0f), subtitle
                );
            }
        }
    }
    tc_ui_painter_pop_clip(context);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
}

size_t ListWidget::index_at(float x, float y) const {
    if (!model_ || !rect_contains(bounds(), x, y)) return SelectionModel::npos;
    const float stride = row_height_ + row_spacing_;
    const float local = y - bounds().y + scroll_y_;
    if (local < 0.0f) return SelectionModel::npos;
    const size_t index = static_cast<size_t>(local / stride);
    if (index >= model_->size() || local - static_cast<float>(index) * stride > row_height_) {
        return SelectionModel::npos;
    }
    return index;
}

bool ListWidget::apply_selection(size_t index, int32_t modifiers) {
    if (index >= model_->size() || !model_->item(index).enabled) return false;
    if ((modifiers & TC_UI_MOD_SHIFT) != 0) {
        select_index(index, false, true, (modifiers & TC_UI_MOD_CTRL) != 0);
        return true;
    }
    select_index(index, (modifiers & TC_UI_MOD_CTRL) != 0, false, false);
    return true;
}

size_t ListWidget::next_enabled(size_t from, int direction) const {
    if (!model_ || model_->empty()) return SelectionModel::npos;
    std::ptrdiff_t index = from == SelectionModel::npos
        ? (direction > 0 ? 0 : static_cast<std::ptrdiff_t>(model_->size()) - 1)
        : static_cast<std::ptrdiff_t>(from) + direction;
    while (index >= 0 && index < static_cast<std::ptrdiff_t>(model_->size())) {
        if (model_->item(static_cast<size_t>(index)).enabled) return static_cast<size_t>(index);
        index += direction;
    }
    return SelectionModel::npos;
}

void ListWidget::emit_selection_changed() {
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    selection_changed_.emit(*this, selection_.selected_indices());
}

tc_ui_event_result ListWidget::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    sync_model();
    if (event->type == TC_UI_POINTER_LEAVE) {
        if (hovered_ != SelectionModel::npos) {
            hovered_ = SelectionModel::npos;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return TC_UI_EVENT_IGNORED;
    }
    if (!rect_contains(bounds(), event->x, event->y)) return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_WHEEL) {
        const float before = scroll_y_;
        set_scroll_y(scroll_y_ - event->wheel_y * row_height_ * wheel_rows_);
        return scroll_y_ != before ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_MOVE) {
        const size_t next = index_at(event->x, event->y);
        if (next != hovered_) {
            hovered_ = next;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN) {
        tc_ui_document_set_focus(document, handle());
        const size_t index = index_at(event->x, event->y);
        if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::RIGHT)) {
            if (index != SelectionModel::npos && model_->item(index).enabled)
                apply_selection(index, event->modifiers);
            context_menu_requested_.emit(
                *this, index == SelectionModel::npos ? -1 : static_cast<int64_t>(index), event->x,
                event->y);
            return TC_UI_EVENT_HANDLED;
        }
        if (index == SelectionModel::npos || !model_->item(index).enabled) {
            if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT))
                activation_clicks_.clear();
            return TC_UI_EVENT_IGNORED;
        }
        const bool selected = apply_selection(index, event->modifiers);
        if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT) &&
            activation_clicks_.press(model_->item(index).stable_id, event->click_count)) {
            activated_.emit(*this, index, model_->item(index));
            return TC_UI_EVENT_HANDLED;
        }
        return selected ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result ListWidget::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN) return TC_UI_EVENT_IGNORED;
    sync_model();
    if (event->key == TC_UI_KEY_A && (event->modifiers & TC_UI_MOD_CTRL) != 0) {
        if (selection_.mode() != SelectionMode::Multiple) return TC_UI_EVENT_IGNORED;
        const std::vector<size_t> before = selection_.selected_indices();
        selection_.clear();
        for (size_t index = 0; index < model_->size(); ++index) {
            if (model_->item(index).enabled) selection_.toggle(index);
        }
        if (before != selection_.selected_indices()) emit_selection_changed();
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_ENTER) {
        const size_t current = selection_.current();
        if (current < model_->size() && model_->item(current).enabled) {
            activated_.emit(*this, current, model_->item(current));
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }
    size_t target = SelectionModel::npos;
    if (event->key == TC_UI_KEY_HOME) target = next_enabled(SelectionModel::npos, 1);
    else if (event->key == TC_UI_KEY_END) target = next_enabled(SelectionModel::npos, -1);
    else if (event->key == TC_UI_KEY_UP_ARROW) target = next_enabled(selection_.current(), -1);
    else if (event->key == TC_UI_KEY_DOWN_ARROW) target = next_enabled(selection_.current(), 1);
    else return TC_UI_EVENT_IGNORED;
    if (target == SelectionModel::npos) return TC_UI_EVENT_HANDLED;
    apply_selection(target, event->modifiers);
    return TC_UI_EVENT_HANDLED;
}


} // namespace termin::gui_native
