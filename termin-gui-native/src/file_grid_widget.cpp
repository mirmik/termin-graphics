#include "widgets_internal.hpp"

#include <cmath>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

namespace {

std::string elide_text(tc_ui_document* document, std::string_view text, float font_size,
                       float max_width) {
    tc_ui_text_metrics metrics{};
    if (!measure_text(document, text, font_size, metrics) || metrics.width <= max_width) {
        return std::string(text);
    }
    constexpr std::string_view ellipsis = "...";
    tc_ui_text_metrics ellipsis_metrics{};
    if (!measure_text(document, ellipsis, font_size, ellipsis_metrics) ||
        ellipsis_metrics.width >= max_width) {
        return std::string(ellipsis);
    }
    const float available = max_width - ellipsis_metrics.width;
    size_t end = text.size();
    while (end > 0) {
        end = utf8_previous_boundary(text, end);
        if (!measure_text(document, text.substr(0, end), font_size, metrics) ||
            metrics.width <= available) {
            break;
        }
    }
    return std::string(text.substr(0, end)) + std::string(ellipsis);
}

tc_ui_color file_icon_color(std::string_view icon) {
    if (icon == "folder") return tc_ui_color{0.80f, 0.67f, 0.24f, 1.0f};
    if (icon == "image") return tc_ui_color{0.31f, 0.73f, 0.47f, 1.0f};
    if (icon == "audio") return tc_ui_color{0.69f, 0.39f, 0.80f, 1.0f};
    if (icon == "video") return tc_ui_color{0.86f, 0.39f, 0.31f, 1.0f};
    if (icon == "archive") return tc_ui_color{0.75f, 0.53f, 0.24f, 1.0f};
    if (icon == "exec") return tc_ui_color{0.31f, 0.78f, 0.57f, 1.0f};
    if (icon == "code") return tc_ui_color{0.39f, 0.61f, 0.88f, 1.0f};
    if (icon == "pdf") return tc_ui_color{0.86f, 0.29f, 0.29f, 1.0f};
    if (icon == "spreadsheet") return tc_ui_color{0.31f, 0.75f, 0.39f, 1.0f};
    return tc_ui_color{0.55f, 0.61f, 0.69f, 1.0f};
}

void draw_semantic_file_icon(tc_ui_paint_context* context, tc_ui_rect rect,
                             std::string_view icon, tc_ui_color tint) {
    if (icon.empty() || rect.width <= 0.0f || rect.height <= 0.0f)
        return;
    tc_ui_color color = file_icon_color(icon);
    color.a *= tint.a;
    if (icon == "folder") {
        const float tab_height = rect.height * 0.27f;
        tc_ui_painter_fill_rounded_rect(
            context, tc_ui_rect{rect.x, rect.y + tab_height * 0.26f, rect.width * 0.52f, tab_height},
            2.0f, color);
        tc_ui_color highlight = color;
        highlight.r = std::min(1.0f, highlight.r * 1.15f);
        highlight.g = std::min(1.0f, highlight.g * 1.15f);
        highlight.b = std::min(1.0f, highlight.b * 1.15f);
        tc_ui_painter_fill_rounded_rect(
            context, tc_ui_rect{rect.x, rect.y + tab_height, rect.width, rect.height - tab_height},
            2.5f, highlight);
        return;
    }
    tc_ui_painter_fill_rounded_rect(context, rect, 2.0f, color);
    const float fold = std::min(rect.width, rect.height) * 0.28f;
    tc_ui_color fold_color = color;
    fold_color.r *= 0.58f;
    fold_color.g *= 0.58f;
    fold_color.b *= 0.58f;
    tc_ui_painter_fill_rect(context, tc_ui_rect{rect.x + rect.width - fold, rect.y, fold, fold}, fold_color);
    tc_ui_color line = color;
    line.r = std::min(1.0f, line.r * 1.35f);
    line.g = std::min(1.0f, line.g * 1.35f);
    line.b = std::min(1.0f, line.b * 1.35f);
    line.a *= 0.65f;
    const float left = rect.x + rect.width * 0.20f;
    const float right = rect.x + rect.width * 0.72f;
    for (int row = 0; row != 3; ++row) {
        const float y = rect.y + rect.height * (0.48f + 0.14f * static_cast<float>(row));
        tc_ui_painter_draw_line(context, tc_ui_point{left, y}, tc_ui_point{right, y}, line, 1.0f);
    }
}

} // namespace

FileGridWidget::FileGridWidget(std::shared_ptr<CollectionModel> model)
    : NativeWidget("FileGridWidget"),
      model_(model ? std::move(model) : std::make_shared<CollectionModel>()) {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_focusable(true);
    set_preferred_size(tc_ui_size{400.0f, 220.0f});
    observed_revision_ = model_->revision();
    connect_model();
}

FileGridWidget::~FileGridWidget() { disconnect_model(); }

void FileGridWidget::connect_model() {
    if (!model_ || model_connection_ != 0)
        return;
    model_connection_ = model_->changed().connect(
        [this](CollectionModel&, const CollectionChange& change) { on_model_changed(change); });
}

void FileGridWidget::disconnect_model() {
    if (model_ && model_connection_ != 0)
        model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void FileGridWidget::set_model(std::shared_ptr<CollectionModel> model) {
    disconnect_model();
    const bool changed = selection_.clear();
    model_ = model ? std::move(model) : std::make_shared<CollectionModel>();
    observed_revision_ = model_->revision();
    scroll_y_ = 0.0f;
    hovered_ = SelectionModel::npos;
    connect_model();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (changed)
        emit_selection_changed();
}

void FileGridWidget::on_model_changed(const CollectionChange& change) {
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
    if (hovered_ >= model_->size())
        hovered_ = SelectionModel::npos;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (selection_changed)
        emit_selection_changed();
}

void FileGridWidget::sync_model() {
    if (model_->revision() == observed_revision_)
        return;
    observed_revision_ = model_->revision();
    const bool changed = selection_.reconcile(model_->size());
    if (hovered_ >= model_->size())
        hovered_ = SelectionModel::npos;
    clamp_scroll();
    if (changed)
        emit_selection_changed();
}

void FileGridWidget::set_selection_mode(SelectionMode mode) {
    const auto before = selection_.selected_indices();
    selection_.set_mode(mode);
    if (before != selection_.selected_indices())
        emit_selection_changed();
}

void FileGridWidget::set_tile_size(float width, float height) {
    if (!std::isfinite(width) || width <= 0.0f || !std::isfinite(height) || height <= 0.0f) {
        tc_log_error("[termin-gui-native] FileGridWidget rejected invalid tile size");
        throw std::invalid_argument("tile size must be finite and positive");
    }
    tile_width_ = width;
    tile_height_ = height;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void FileGridWidget::set_tile_spacing(float spacing) {
    if (!std::isfinite(spacing) || spacing < 0.0f) {
        tc_log_error("[termin-gui-native] FileGridWidget rejected invalid tile spacing");
        throw std::invalid_argument("tile spacing must be finite and non-negative");
    }
    tile_spacing_ = spacing;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void FileGridWidget::set_padding(float padding) {
    if (!std::isfinite(padding) || padding < 0.0f) {
        tc_log_error("[termin-gui-native] FileGridWidget rejected invalid padding");
        throw std::invalid_argument("padding must be finite and non-negative");
    }
    padding_ = padding;
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void FileGridWidget::set_icon_size(float size) {
    if (!std::isfinite(size) || size < 0.0f) {
        tc_log_error("[termin-gui-native] FileGridWidget rejected invalid icon size");
        throw std::invalid_argument("icon size must be finite and non-negative");
    }
    icon_size_ = size;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void FileGridWidget::set_show_scrollbar(bool show) {
    show_scrollbar_ = show;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void FileGridWidget::set_scrollbar_width(float width) {
    if (!std::isfinite(width) || width <= 0.0f) {
        tc_log_error("[termin-gui-native] FileGridWidget rejected invalid scrollbar width");
        throw std::invalid_argument("scrollbar width must be finite and positive");
    }
    scrollbar_width_ = width;
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

void FileGridWidget::set_empty_text(std::string text) {
    if (!valid_utf8(text)) {
        tc_log_error("[termin-gui-native] FileGridWidget rejected invalid UTF-8 empty text");
        throw std::invalid_argument("empty text must be valid UTF-8");
    }
    empty_text_ = std::move(text);
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

size_t FileGridWidget::column_count() const {
    const float usable = std::max(1.0f, bounds().width - padding_ * 2.0f);
    return std::max<size_t>(
        1, static_cast<size_t>((usable + tile_spacing_) / (tile_width_ + tile_spacing_)));
}

size_t FileGridWidget::row_count() const {
    return model_->empty() ? 0 : (model_->size() + column_count() - 1) / column_count();
}

float FileGridWidget::content_height() const {
    const size_t rows = row_count();
    if (rows == 0)
        return padding_ * 2.0f + tile_height_;
    return padding_ * 2.0f + static_cast<float>(rows) * tile_height_ +
           static_cast<float>(rows - 1) * tile_spacing_;
}

float FileGridWidget::max_scroll() const {
    return std::max(0.0f, content_height() - bounds().height);
}

void FileGridWidget::clamp_scroll() { scroll_y_ = clamp_float(scroll_y_, 0.0f, max_scroll()); }

void FileGridWidget::set_scroll_y(float offset) {
    if (!std::isfinite(offset)) {
        tc_log_error("[termin-gui-native] FileGridWidget rejected non-finite scroll offset");
        throw std::invalid_argument("scroll offset must be finite");
    }
    const float before = scroll_y_;
    scroll_y_ = offset;
    clamp_scroll();
    if (before != scroll_y_)
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

bool FileGridWidget::has_scrollbar() const { return show_scrollbar_ && max_scroll() > 0.0f; }

tc_ui_rect FileGridWidget::scrollbar_thumb_rect() const {
    if (!has_scrollbar())
        return {};
    const float viewport_ratio = bounds().height / std::max(content_height(), bounds().height);
    const float thumb_height = std::max(20.0f, bounds().height * viewport_ratio);
    const float track_height = std::max(0.0f, bounds().height - thumb_height);
    const float y =
        bounds().y + (max_scroll() > 0.0f ? track_height * scroll_y_ / max_scroll() : 0.0f);
    return tc_ui_rect{bounds().x + bounds().width - scrollbar_width_, y, scrollbar_width_,
                      thumb_height};
}

bool FileGridWidget::scrollbar_hit(float x, float y) const {
    return has_scrollbar() &&
           rect_contains(tc_ui_rect{bounds().x + bounds().width - scrollbar_width_, bounds().y,
                                    scrollbar_width_, bounds().height},
                         x, y);
}

tc_ui_rect FileGridWidget::item_rect(size_t index) const {
    const size_t columns = column_count();
    const size_t column = index % columns;
    const size_t row = index / columns;
    return tc_ui_rect{bounds().x + padding_ +
                          static_cast<float>(column) * (tile_width_ + tile_spacing_),
                      bounds().y + padding_ +
                          static_cast<float>(row) * (tile_height_ + tile_spacing_) - scroll_y_,
                      tile_width_, tile_height_};
}

std::pair<size_t, size_t> FileGridWidget::visible_range() const {
    if (model_->empty() || bounds().height <= 0.0f)
        return {0, 0};
    const float stride = tile_height_ + tile_spacing_;
    const size_t first_row =
        static_cast<size_t>(std::max(0.0f, std::floor((scroll_y_ - padding_) / stride)));
    const size_t last_row =
        static_cast<size_t>(std::ceil((scroll_y_ + bounds().height) / stride)) + 1;
    const size_t columns = column_count();
    return {std::min(model_->size(), first_row * columns),
            std::min(model_->size(), last_row * columns)};
}

void FileGridWidget::ensure_visible(size_t index) {
    if (index >= model_->size())
        return;
    const tc_ui_rect rect = item_rect(index);
    const float top = rect.y - bounds().y + scroll_y_;
    const float bottom = top + rect.height;
    if (top < scroll_y_)
        set_scroll_y(top);
    else if (bottom > scroll_y_ + bounds().height)
        set_scroll_y(bottom - bounds().height);
}

bool FileGridWidget::select_index(size_t index, bool toggle, bool extend, bool additive) {
    sync_model();
    if (index >= model_->size() || !model_->item(index).enabled)
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

bool FileGridWidget::clear_selection() {
    const bool changed = selection_.clear();
    if (changed)
        emit_selection_changed();
    return changed;
}

tc_ui_size FileGridWidget::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    sync_model();
    const tc_ui_style style = computed_style(document);
    return clamp_size(tc_ui_size{std::max(preferred_size().width, style.min_width),
                                 std::max(preferred_size().height, style.min_height)},
                      constraints);
}

void FileGridWidget::layout(tc_ui_document* document, tc_ui_rect rect) {
    sync_model();
    NativeWidget::layout(document, rect);
    clamp_scroll();
}

void FileGridWidget::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    sync_model();
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_push_clip(context, bounds());
    if (model_->empty()) {
        tc_ui_color muted = style.foreground;
        muted.a *= 0.6f;
        tc_ui_painter_draw_text(
            context, empty_text_.c_str(),
            tc_ui_point{bounds().x + padding_, bounds().y + padding_ + style.font_size},
            style.font_size, muted);
    } else {
        const auto [first, last] = visible_range();
        for (size_t index = first; index < last; ++index) {
            const CollectionItem& item = model_->item(index);
            const tc_ui_rect tile = item_rect(index);
            tc_ui_painter_push_clip(context, tile);
            if (selection_.contains(index) || hovered_ == index) {
                tc_ui_color highlight = style.accent;
                highlight.a *= selection_.contains(index) ? 0.42f : 0.20f;
                tc_ui_painter_fill_rounded_rect(context, tile, 5.0f, highlight);
            }
            tc_ui_color foreground = style.foreground;
            if (!item.enabled)
                foreground.a *= 0.45f;
            if (icon_size_ > 0.0f) {
                const tc_ui_rect icon_rect{tile.x + (tile.width - icon_size_) * 0.5f,
                                           tile.y + 8.0f, icon_size_, icon_size_};
                if (item.texture_id != 0) {
                    tc_ui_painter_draw_texture(context, item.texture_id, icon_rect, foreground,
                                               TC_UI_TEXTURE_SAMPLING_LINEAR, false);
                } else {
                    draw_semantic_file_icon(context, icon_rect, item.icon, foreground);
                }
            }
            const float name_size = style.font_size;
            const float subtitle_size = std::max(9.0f, style.font_size - 2.0f);
            const std::string name = elide_text(document, item.text, name_size, tile.width - 10.0f);
            tc_ui_text_metrics metrics{};
            measure_text(document, name, name_size, metrics);
            tc_ui_painter_draw_text(
                context, name.c_str(),
                tc_ui_point{tile.x + std::max(5.0f, (tile.width - metrics.width) * 0.5f),
                            tile.y + 8.0f + icon_size_ + name_size + 5.0f},
                name_size, foreground);
            if (!item.subtitle.empty()) {
                tc_ui_color subtitle = foreground;
                subtitle.a *= 0.65f;
                const std::string subtitle_text =
                    elide_text(document, item.subtitle, subtitle_size, tile.width - 10.0f);
                metrics = {};
                measure_text(document, subtitle_text, subtitle_size, metrics);
                tc_ui_painter_draw_text(
                    context, subtitle_text.c_str(),
                    tc_ui_point{tile.x + std::max(5.0f, (tile.width - metrics.width) * 0.5f),
                                tile.y + tile.height - 6.0f},
                    subtitle_size, subtitle);
            }
            tc_ui_painter_pop_clip(context);
        }
    }
    if (has_scrollbar()) {
        tc_ui_color thumb = style.border;
        if (dragging_scrollbar_)
            thumb = style.accent;
        tc_ui_painter_fill_rounded_rect(context, scrollbar_thumb_rect(), scrollbar_width_ * 0.5f,
                                        thumb);
    }
    tc_ui_painter_pop_clip(context);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
}

size_t FileGridWidget::index_at(float x, float y) const {
    if (!rect_contains(bounds(), x, y) || scrollbar_hit(x, y))
        return SelectionModel::npos;
    const float local_x = x - bounds().x - padding_;
    const float local_y = y - bounds().y - padding_ + scroll_y_;
    if (local_x < 0.0f || local_y < 0.0f)
        return SelectionModel::npos;
    const float column_stride = tile_width_ + tile_spacing_;
    const float row_stride = tile_height_ + tile_spacing_;
    const size_t column = static_cast<size_t>(local_x / column_stride);
    const size_t row = static_cast<size_t>(local_y / row_stride);
    if (column >= column_count() ||
        local_x - static_cast<float>(column) * column_stride > tile_width_ ||
        local_y - static_cast<float>(row) * row_stride > tile_height_) {
        return SelectionModel::npos;
    }
    const size_t index = row * column_count() + column;
    return index < model_->size() ? index : SelectionModel::npos;
}

bool FileGridWidget::apply_selection(size_t index, int32_t modifiers) {
    if (index >= model_->size() || !model_->item(index).enabled)
        return false;
    if ((modifiers & TC_UI_MOD_SHIFT) != 0) {
        select_index(index, false, true, (modifiers & TC_UI_MOD_CTRL) != 0);
    } else {
        select_index(index, (modifiers & TC_UI_MOD_CTRL) != 0, false, false);
    }
    return true;
}

size_t FileGridWidget::next_enabled(size_t from, int direction) const {
    if (model_->empty())
        return SelectionModel::npos;
    std::ptrdiff_t index =
        from == SelectionModel::npos
            ? (direction > 0 ? 0 : static_cast<std::ptrdiff_t>(model_->size()) - 1)
            : static_cast<std::ptrdiff_t>(from) + direction;
    while (index >= 0 && index < static_cast<std::ptrdiff_t>(model_->size())) {
        if (model_->item(static_cast<size_t>(index)).enabled)
            return static_cast<size_t>(index);
        index += direction;
    }
    return SelectionModel::npos;
}

tc_ui_event_result FileGridWidget::pointer_event(tc_ui_document* document,
                                                 const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    sync_model();
    if (dragging_scrollbar_) {
        if (event->type == TC_UI_POINTER_MOVE) {
            const tc_ui_rect thumb = scrollbar_thumb_rect();
            const float track = bounds().height - thumb.height;
            if (track > 0.0f) {
                set_scroll_y(drag_start_scroll_ +
                             (event->y - drag_start_y_) * max_scroll() / track);
            }
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_UP) {
            dragging_scrollbar_ = false;
            tc_ui_document_release_pointer_capture(document, handle());
            return TC_UI_EVENT_HANDLED;
        }
    }
    if (pressed_item_ != SelectionModel::npos) {
        if (event->type == TC_UI_POINTER_DOWN) {
            pressed_item_ = SelectionModel::npos;
            dragging_item_ = false;
            tc_ui_document_release_pointer_capture(document, handle());
        }
        if (event->type == TC_UI_POINTER_MOVE) {
            const float dx = event->x - item_press_x_;
            const float dy = event->y - item_press_y_;
            if (!dragging_item_ && dx * dx + dy * dy >= 16.0f) {
                dragging_item_ = true;
                mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            }
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_UP) {
            const size_t dragged = pressed_item_;
            const bool was_dragging = dragging_item_;
            pressed_item_ = SelectionModel::npos;
            dragging_item_ = false;
            tc_ui_document_release_pointer_capture(document, handle());
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            if (was_dragging && dragged < model_->size())
                drag_requested_.emit(*this, dragged, event->x, event->y, event->modifiers);
            return TC_UI_EVENT_HANDLED;
        }
    }
    if (event->type == TC_UI_POINTER_LEAVE) {
        hovered_ = SelectionModel::npos;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_IGNORED;
    }
    if (!rect_contains(bounds(), event->x, event->y))
        return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_WHEEL) {
        const float before = scroll_y_;
        set_scroll_y(scroll_y_ - event->wheel_y * 30.0f);
        return before != scroll_y_ ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_MOVE) {
        const size_t next = index_at(event->x, event->y);
        if (next != hovered_) {
            hovered_ = next;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type != TC_UI_POINTER_DOWN)
        return TC_UI_EVENT_IGNORED;
    tc_ui_document_set_focus(document, handle());
    if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT) &&
        scrollbar_hit(event->x, event->y)) {
        dragging_scrollbar_ = true;
        drag_start_y_ = event->y;
        drag_start_scroll_ = scroll_y_;
        tc_ui_document_set_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    const size_t index = index_at(event->x, event->y);
    if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::RIGHT)) {
        if (index != SelectionModel::npos)
            apply_selection(index, event->modifiers);
        context_menu_requested_.emit(
            *this, index == SelectionModel::npos ? -1 : static_cast<int64_t>(index), event->x,
            event->y);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->button != tcbase::mouse_button_value(tcbase::MouseButton::LEFT))
        return TC_UI_EVENT_IGNORED;
    if (index == SelectionModel::npos || !model_->item(index).enabled) {
        activation_clicks_.clear();
        return TC_UI_EVENT_IGNORED;
    }
    apply_selection(index, event->modifiers);
    if (activation_clicks_.press(model_->item(index).stable_id, event->click_count)) {
        activated_.emit(*this, index, model_->item(index));
        return TC_UI_EVENT_HANDLED;
    }
    pressed_item_ = index;
    dragging_item_ = false;
    item_press_x_ = event->x;
    item_press_y_ = event->y;
    tc_ui_document_set_pointer_capture(document, handle());
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result FileGridWidget::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN)
        return TC_UI_EVENT_IGNORED;
    sync_model();
    const size_t current = selection_.current();
    if (event->key == TC_UI_KEY_ENTER || event->key == TC_UI_KEY_DELETE) {
        if (current >= model_->size() || !model_->item(current).enabled)
            return TC_UI_EVENT_IGNORED;
        if (event->key == TC_UI_KEY_ENTER)
            activated_.emit(*this, current, model_->item(current));
        else
            delete_requested_.emit(*this, current, model_->item(current));
        return TC_UI_EVENT_HANDLED;
    }
    size_t target = SelectionModel::npos;
    if (event->key == TC_UI_KEY_HOME)
        target = next_enabled(SelectionModel::npos, 1);
    else if (event->key == TC_UI_KEY_END)
        target = next_enabled(SelectionModel::npos, -1);
    else if (event->key == TC_UI_KEY_LEFT)
        target = next_enabled(current, -1);
    else if (event->key == TC_UI_KEY_RIGHT)
        target = next_enabled(current, 1);
    else if (event->key == TC_UI_KEY_UP_ARROW) {
        const size_t step = column_count();
        target = current == SelectionModel::npos || current < step
                     ? next_enabled(SelectionModel::npos, 1)
                     : next_enabled(current - step + 1, -1);
    } else if (event->key == TC_UI_KEY_DOWN_ARROW) {
        const size_t step = column_count();
        if (current == SelectionModel::npos)
            target = next_enabled(SelectionModel::npos, 1);
        else if (current + step < model_->size())
            target = next_enabled(current + step - 1, 1);
    } else
        return TC_UI_EVENT_IGNORED;
    if (target != SelectionModel::npos)
        apply_selection(target, event->modifiers);
    return TC_UI_EVENT_HANDLED;
}

void FileGridWidget::emit_selection_changed() {
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    selection_changed_.emit(*this, selection_.selected_indices());
}

} // namespace termin::gui_native
