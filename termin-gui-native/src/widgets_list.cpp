#include "widgets_internal.hpp"

#include <cmath>
#include <limits>

namespace termin::gui_native {
using namespace detail;

namespace {

bool same_selection(const std::vector<size_t>& lhs, const std::vector<size_t>& rhs) {
    return lhs == rhs;
}

} // namespace

const CollectionItem& CollectionModel::item(size_t index) const {
    if (index >= items_.size()) {
        throw std::out_of_range("collection item index out of range");
    }
    return items_[index];
}

void CollectionModel::validate_item(const CollectionItem& item) {
    if (!valid_utf8(item.stable_id) || !valid_utf8(item.text) || !valid_utf8(item.subtitle)) {
        tc_log_error("[termin-gui-native] collection model rejected invalid UTF-8 item");
        throw std::invalid_argument("collection item strings must be valid UTF-8");
    }
}

void CollectionModel::set_items(std::vector<CollectionItem> items) {
    for (const CollectionItem& item : items) validate_item(item);
    items_ = std::move(items);
    notify(CollectionChange {CollectionChangeKind::Reset, 0, items_.size()});
}

void CollectionModel::append(CollectionItem item) {
    validate_item(item);
    const size_t index = items_.size();
    items_.push_back(std::move(item));
    notify(CollectionChange {CollectionChangeKind::Insert, index, 1});
}

void CollectionModel::update(size_t index, CollectionItem item) {
    validate_item(item);
    if (index >= items_.size()) {
        throw std::out_of_range("collection item index out of range");
    }
    items_[index] = std::move(item);
    notify(CollectionChange {CollectionChangeKind::Update, index, 1});
}

void CollectionModel::erase(size_t index) {
    if (index >= items_.size()) {
        throw std::out_of_range("collection item index out of range");
    }
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    notify(CollectionChange {CollectionChangeKind::Erase, index, 1});
}

void CollectionModel::clear() {
    if (items_.empty()) return;
    const size_t count = items_.size();
    items_.clear();
    notify(CollectionChange {CollectionChangeKind::Reset, 0, count});
}

void CollectionModel::notify(CollectionChange change) {
    ++revision_;
    changed_.emit(*this, change);
}

void SelectionModel::normalize() {
    std::sort(selected_.begin(), selected_.end());
    selected_.erase(std::unique(selected_.begin(), selected_.end()), selected_.end());
    if (mode_ == SelectionMode::Single && selected_.size() > 1) {
        const size_t keep = contains(current_) ? current_ : selected_.front();
        selected_.assign(1, keep);
    }
}

bool SelectionModel::contains(size_t index) const {
    return std::binary_search(selected_.begin(), selected_.end(), index);
}

void SelectionModel::set_mode(SelectionMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    normalize();
    if (selected_.empty()) {
        current_ = npos;
        anchor_ = npos;
    } else {
        current_ = selected_.front();
        if (!contains(anchor_)) anchor_ = selected_.front();
    }
}

bool SelectionModel::reconcile(size_t item_count) {
    const std::vector<size_t> before = selected_;
    selected_.erase(
        std::remove_if(selected_.begin(), selected_.end(), [item_count](size_t index) {
            return index >= item_count;
        }),
        selected_.end()
    );
    if (current_ >= item_count) current_ = item_count == 0 ? npos : item_count - 1;
    if (anchor_ >= item_count) anchor_ = selected_.empty() ? npos : selected_.front();
    return !same_selection(before, selected_);
}

bool SelectionModel::clear() {
    if (selected_.empty() && current_ == npos && anchor_ == npos) return false;
    selected_.clear();
    current_ = npos;
    anchor_ = npos;
    return true;
}

bool SelectionModel::select_only(size_t index) {
    if (index == npos) {
        tc_log_error("[termin-gui-native] selection model rejected invalid index");
        throw std::invalid_argument("selection index must not be npos");
    }
    const bool changed = selected_.size() != 1 || selected_.front() != index;
    selected_.assign(1, index);
    current_ = index;
    anchor_ = index;
    return changed;
}

bool SelectionModel::toggle(size_t index) {
    if (index == npos) {
        tc_log_error("[termin-gui-native] selection model rejected invalid index");
        throw std::invalid_argument("selection index must not be npos");
    }
    if (mode_ == SelectionMode::Single) return select_only(index);
    const auto found = std::lower_bound(selected_.begin(), selected_.end(), index);
    bool changed = true;
    if (found != selected_.end() && *found == index) selected_.erase(found);
    else selected_.insert(found, index);
    current_ = index;
    anchor_ = index;
    return changed;
}

bool SelectionModel::extend_to(size_t index, bool additive) {
    if (index == npos) {
        tc_log_error("[termin-gui-native] selection model rejected invalid index");
        throw std::invalid_argument("selection index must not be npos");
    }
    if (mode_ == SelectionMode::Single) return select_only(index);
    if (anchor_ == npos) anchor_ = current_ == npos ? index : current_;
    const std::vector<size_t> before = selected_;
    if (!additive) selected_.clear();
    const size_t first = std::min(anchor_, index);
    const size_t last = std::max(anchor_, index);
    for (size_t value = first; value <= last; ++value) selected_.push_back(value);
    current_ = index;
    normalize();
    return !same_selection(before, selected_);
}

bool SelectionModel::select_all(size_t item_count) {
    if (mode_ != SelectionMode::Multiple) return false;
    const std::vector<size_t> before = selected_;
    selected_.resize(item_count);
    for (size_t index = 0; index < item_count; ++index) selected_[index] = index;
    current_ = item_count == 0 ? npos : item_count - 1;
    anchor_ = item_count == 0 ? npos : 0;
    return !same_selection(before, selected_);
}

bool SelectionModel::items_inserted(size_t index, size_t count) {
    if (count == 0) return false;
    const std::vector<size_t> before = selected_;
    for (size_t& selected : selected_) {
        if (selected >= index) selected += count;
    }
    if (current_ != npos && current_ >= index) current_ += count;
    if (anchor_ != npos && anchor_ >= index) anchor_ += count;
    return before != selected_;
}

bool SelectionModel::items_erased(size_t index, size_t count, size_t remaining_count) {
    if (count == 0) return false;
    const size_t end = index > std::numeric_limits<size_t>::max() - count
        ? std::numeric_limits<size_t>::max()
        : index + count;
    const std::vector<size_t> before = selected_;
    selected_.erase(
        std::remove_if(selected_.begin(), selected_.end(), [index, end](size_t selected) {
            return selected >= index && selected < end;
        }),
        selected_.end()
    );
    for (size_t& selected : selected_) {
        if (selected >= end) selected -= count;
    }
    const auto shift_position = [index, end, count, remaining_count](size_t position) {
        if (position == npos) return npos;
        if (position >= end) return position - count;
        if (position >= index) return remaining_count == 0 ? npos : std::min(index, remaining_count - 1);
        return position;
    };
    current_ = shift_position(current_);
    anchor_ = shift_position(anchor_);
    return before != selected_;
}

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
        return index != SelectionModel::npos && apply_selection(index, event->modifiers)
            ? TC_UI_EVENT_HANDLED
            : TC_UI_EVENT_IGNORED;
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
