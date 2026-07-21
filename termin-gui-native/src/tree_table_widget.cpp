#include "widgets_internal.hpp"

#include <cmath>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

TreeTableWidget::TreeTableWidget(std::shared_ptr<TreeTableModel> model,
                                 std::shared_ptr<TableColumnModel> columns,
                                 std::shared_ptr<TreeExpansionModel> expansion)
    : NativeWidget("TreeTableWidget"),
      model_(model ? std::move(model) : std::make_shared<TreeTableModel>()),
      columns_(columns ? std::move(columns)
                       : std::make_shared<TableColumnModel>()),
      expansion_(expansion ? std::move(expansion)
                           : std::make_shared<TreeExpansionModel>()) {
  set_style_role(TC_UI_STYLE_TEXT_INPUT);
  set_focusable(true);
  set_preferred_size(tc_ui_size{400.0f, 220.0f});
  observed_model_revision_ = model_->revision();
  observed_column_revision_ = columns_->revision();
  observed_expansion_revision_ = expansion_->revision();
  connect_models();
  rebuild_visible();
}

TreeTableWidget::~TreeTableWidget() { disconnect_models(); }

void TreeTableWidget::connect_models() {
  if (model_ && model_connection_ == 0) {
    model_connection_ = model_->changed().connect(
        [this](TreeTableModel &) { on_model_changed(); });
  }
  if (columns_ && column_connection_ == 0) {
    column_connection_ = columns_->changed().connect(
        [this](TableColumnModel &, const TableColumnChange &) {
          on_columns_changed();
        });
  }
  if (expansion_ && expansion_connection_ == 0) {
    expansion_connection_ = expansion_->changed().connect(
        [this](TreeExpansionModel &, TreeNodeId node, bool value) {
          on_expansion_changed(node, value);
        });
  }
}

void TreeTableWidget::disconnect_models() {
  if (model_ && model_connection_ != 0)
    model_->changed().disconnect(model_connection_);
  if (columns_ && column_connection_ != 0)
    columns_->changed().disconnect(column_connection_);
  if (expansion_ && expansion_connection_ != 0)
    expansion_->changed().disconnect(expansion_connection_);
  model_connection_ = 0;
  column_connection_ = 0;
  expansion_connection_ = 0;
}

void TreeTableWidget::set_model(std::shared_ptr<TreeTableModel> model) {
  disconnect_models();
  const bool selection_changed = selected_ != kInvalidTreeTableNodeId;
  model_ = model ? std::move(model) : std::make_shared<TreeTableModel>();
  selected_ = kInvalidTreeTableNodeId;
  hovered_ = kInvalidTreeTableNodeId;
  scroll_y_ = 0.0f;
  observed_model_revision_ = model_->revision();
  connect_models();
  rebuild_visible();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
  if (selection_changed)
    selection_changed_.emit(*this, selected_);
}

void TreeTableWidget::set_column_model(
    std::shared_ptr<TableColumnModel> columns) {
  disconnect_models();
  columns_ =
      columns ? std::move(columns) : std::make_shared<TableColumnModel>();
  observed_column_revision_ = columns_->revision();
  resizing_column_ = SIZE_MAX;
  connect_models();
  compute_column_layout();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
}

void TreeTableWidget::set_expansion_model(
    std::shared_ptr<TreeExpansionModel> expansion) {
  disconnect_models();
  expansion_ =
      expansion ? std::move(expansion) : std::make_shared<TreeExpansionModel>();
  observed_expansion_revision_ = expansion_->revision();
  connect_models();
  rebuild_visible();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
}

void TreeTableWidget::on_model_changed() {
  observed_model_revision_ = model_->revision();
  expansion_->reconcile(*model_);
  if (!model_->contains(selected_)) {
    const bool changed = selected_ != kInvalidTreeTableNodeId;
    selected_ = kInvalidTreeTableNodeId;
    if (changed)
      selection_changed_.emit(*this, selected_);
  }
  if (!model_->contains(hovered_))
    hovered_ = kInvalidTreeTableNodeId;
  rebuild_visible();
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
}

void TreeTableWidget::on_columns_changed() {
  observed_column_revision_ = columns_->revision();
  if (resizing_column_ >= columns_->size())
    resizing_column_ = SIZE_MAX;
  compute_column_layout();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
}

void TreeTableWidget::on_expansion_changed(TreeTableNodeId node,
                                           bool expanded) {
  observed_expansion_revision_ = expansion_->revision();
  rebuild_visible();
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
  expansion_changed_.emit(*this, node, expanded);
}

void TreeTableWidget::sync_models() {
  if (model_->revision() != observed_model_revision_)
    on_model_changed();
  if (columns_->revision() != observed_column_revision_)
    on_columns_changed();
  if (expansion_->revision() != observed_expansion_revision_) {
    observed_expansion_revision_ = expansion_->revision();
    rebuild_visible();
    clamp_scroll();
  }
}

void TreeTableWidget::rebuild_visible() {
  visible_ = build_tree_projection(
      model_->roots(),
      [this](TreeNodeId node) -> const std::vector<TreeNodeId> & {
        return model_->node(node).children;
      },
      [this](TreeNodeId node) { return expansion_->expanded(node); },
      model_->size());
}

bool TreeTableWidget::select_node(TreeTableNodeId node, bool reveal) {
  sync_models();
  if (!model_->contains(node) || !model_->node(node).data.enabled)
    return false;
  const bool changed = selected_ != node;
  selected_ = node;
  if (reveal)
    ensure_visible(node);
  if (changed) {
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    selection_changed_.emit(*this, selected_);
  }
  return changed;
}

bool TreeTableWidget::clear_selection() {
  if (selected_ == kInvalidTreeTableNodeId)
    return false;
  selected_ = kInvalidTreeTableNodeId;
  mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
  selection_changed_.emit(*this, selected_);
  return true;
}

bool TreeTableWidget::set_expanded(TreeTableNodeId node, bool expanded) {
  if (!model_->contains(node) || model_->node(node).children.empty())
    return false;
  return expansion_->set_expanded(node, expanded);
}

bool TreeTableWidget::toggle(TreeTableNodeId node) {
  return set_expanded(node, !expansion_->expanded(node));
}

void TreeTableWidget::set_row_height(float height) {
  if (!std::isfinite(height) || height <= 0.0f) {
    tc_log_error(
        "[termin-gui-native] TreeTableWidget rejected invalid row height");
    throw std::invalid_argument("row height must be finite and positive");
  }
  row_height_ = height;
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TreeTableWidget::set_header_height(float height) {
  if (!std::isfinite(height) || height <= 0.0f) {
    tc_log_error(
        "[termin-gui-native] TreeTableWidget rejected invalid header height");
    throw std::invalid_argument("header height must be finite and positive");
  }
  header_height_ = height;
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TreeTableWidget::set_indent_size(float size) {
  if (!std::isfinite(size) || size < 0.0f) {
    tc_log_error(
        "[termin-gui-native] TreeTableWidget rejected invalid indentation");
    throw std::invalid_argument("indentation must be finite and non-negative");
  }
  indent_size_ = size;
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

float TreeTableWidget::body_height() const {
  return std::max(0.0f, bounds().height - header_height_);
}

float TreeTableWidget::content_height() const {
  return static_cast<float>(visible_.size()) * row_height_;
}

void TreeTableWidget::clamp_scroll() {
  scroll_y_ = clamp_float(scroll_y_, 0.0f,
                          std::max(0.0f, content_height() - body_height()));
}

void TreeTableWidget::set_scroll_y(float offset) {
  if (!std::isfinite(offset)) {
    tc_log_error("[termin-gui-native] TreeTableWidget rejected non-finite "
                 "scroll offset");
    throw std::invalid_argument("scroll offset must be finite");
  }
  const float before = scroll_y_;
  scroll_y_ = offset;
  clamp_scroll();
  if (before != scroll_y_)
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

size_t TreeTableWidget::visible_count() {
  sync_models();
  return visible_.size();
}

std::pair<size_t, size_t> TreeTableWidget::visible_range() {
  sync_models();
  if (visible_.empty() || body_height() <= 0.0f)
    return {0, 0};
  const size_t first = std::min(
      visible_.size(),
      static_cast<size_t>(std::max(0.0f, std::floor(scroll_y_ / row_height_))));
  const size_t last = std::min(visible_.size(),
                               static_cast<size_t>(std::ceil(
                                   (scroll_y_ + body_height()) / row_height_)) +
                                   1);
  return {first, last};
}

TreeVisibleRow TreeTableWidget::visible_row(size_t index) {
  sync_models();
  if (index >= visible_.size())
    throw std::out_of_range("tree-table visible row index out of range");
  return visible_[index];
}

size_t TreeTableWidget::visible_index(TreeTableNodeId node) const {
  for (size_t index = 0; index < visible_.size(); ++index) {
    if (visible_[index].node == node)
      return index;
  }
  return SIZE_MAX;
}

void TreeTableWidget::ensure_visible(TreeTableNodeId node) {
  if (!model_->contains(node))
    return;
  std::vector<TreeTableNodeId> ancestors;
  TreeTableNodeId parent = model_->node(node).parent;
  while (parent != kInvalidTreeTableNodeId) {
    ancestors.push_back(parent);
    parent = model_->node(parent).parent;
  }
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
    set_expanded(*it, true);
  sync_models();
  const size_t index = visible_index(node);
  if (index == SIZE_MAX)
    return;
  const float top = static_cast<float>(index) * row_height_;
  const float bottom = top + row_height_;
  if (top < scroll_y_)
    set_scroll_y(top);
  else if (bottom > scroll_y_ + body_height())
    set_scroll_y(bottom - body_height());
}

void TreeTableWidget::compute_column_layout() {
  column_layout_.assign(columns_->size(), TableColumnLayout{});
  if (columns_->empty())
    return;
  float fixed_total = 0.0f;
  float stretch_min_total = 0.0f;
  float stretch_weight = 0.0f;
  for (size_t index = 0; index < columns_->size(); ++index) {
    const TableColumn &column = columns_->column(index);
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
  float extra =
      std::max(0.0f, bounds().width - fixed_total - stretch_min_total);
  std::vector<bool> active(columns_->size(), true);
  while (extra > 0.001f && stretch_weight > 0.0f) {
    float consumed = 0.0f;
    float next_weight = 0.0f;
    for (size_t index = 0; index < columns_->size(); ++index) {
      const TableColumn &column = columns_->column(index);
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
  for (TableColumnLayout &column : column_layout_) {
    column.x = x;
    x += column.width;
  }
}

tc_ui_size TreeTableWidget::measure(tc_ui_document_handle document,
                                    tc_ui_constraints constraints) {
  sync_models();
  const tc_ui_style style = computed_style(document);
  return clamp_size(
      tc_ui_size{std::max(preferred_size().width, style.min_width),
                 std::max(preferred_size().height, style.min_height)},
      constraints);
}

void TreeTableWidget::layout(tc_ui_document_handle document, tc_ui_rect rect) {
  sync_models();
  NativeWidget::layout(document, rect);
  compute_column_layout();
  clamp_scroll();
}

void TreeTableWidget::paint(tc_ui_document_handle document,
                            tc_ui_paint_context *context) {
  sync_models();
  const tc_ui_style style = computed_style(document);
  const tc_ui_rect header{bounds().x, bounds().y, bounds().width,
                          header_height_};
  tc_ui_color header_color = style.accent;
  header_color.a *= 0.20f;
  tc_ui_painter_fill_rect(context, bounds(), style.background);
  tc_ui_painter_push_clip(context, bounds());
  tc_ui_painter_fill_rect(context, header, header_color);
  for (size_t index = 0; index < columns_->size(); ++index) {
    const TableColumn &column = columns_->column(index);
    const TableColumnLayout layout = column_layout_[index];
    const tc_ui_rect cell{bounds().x + layout.x, bounds().y, layout.width,
                          header_height_};
    tc_ui_painter_push_clip(context, cell);
    tc_ui_painter_draw_text(
        context, column.header.c_str(),
        tc_ui_point{cell.x + cell_padding_, cell.y + cell.height * 0.66f},
        std::max(9.0f, style.font_size - 1.0f), style.foreground);
    tc_ui_painter_pop_clip(context);
    if (index + 1 < columns_->size()) {
      const float separator = cell.x + cell.width;
      tc_ui_painter_draw_line(
          context, tc_ui_point{separator, cell.y + 2.0f},
          tc_ui_point{separator, cell.y + cell.height - 2.0f}, style.border,
          style.border_width);
    }
  }
  tc_ui_painter_draw_line(
      context, tc_ui_point{header.x, header.y + header.height},
      tc_ui_point{header.x + header.width, header.y + header.height},
      style.border, style.border_width);

  const tc_ui_rect body{bounds().x, bounds().y + header_height_, bounds().width,
                        body_height()};
  tc_ui_painter_push_clip(context, body);
  if (visible_.empty()) {
    tc_ui_color muted = style.foreground;
    muted.a *= 0.6f;
    tc_ui_painter_draw_text(
        context, "No data",
        tc_ui_point{body.x + cell_padding_, body.y + row_height_ * 0.66f},
        style.font_size, muted);
  } else {
    const auto [first, last] = visible_range();
    for (size_t index = first; index < last; ++index) {
      const TreeVisibleRow row_info = visible_[index];
      const TreeTableNode &node = model_->node(row_info.node);
      const float y =
          body.y + static_cast<float>(index) * row_height_ - scroll_y_;
      const tc_ui_rect row{body.x, y, body.width, row_height_};
      tc_ui_color row_color = index % 2 == 0 ? style.background : style.border;
      row_color.a = index % 2 == 0 ? 0.18f : 0.10f;
      if (selected_ == node.id || hovered_ == node.id) {
        row_color = style.accent;
        row_color.a *= selected_ == node.id ? 0.42f : 0.20f;
      }
      tc_ui_painter_fill_rect(context, row, row_color);
      tc_ui_color foreground = style.foreground;
      if (!node.data.enabled)
        foreground.a *= 0.45f;
      const size_t cell_count =
          std::min(node.data.cells.size(), column_layout_.size());
      for (size_t column = 0; column < cell_count; ++column) {
        const TableColumnLayout layout = column_layout_[column];
        const tc_ui_rect cell{row.x + layout.x, row.y, layout.width,
                              row.height};
        tc_ui_painter_push_clip(context, cell);
        float text_x = cell.x + cell_padding_;
        if (column == 0) {
          const float toggle_x =
              text_x + static_cast<float>(row_info.depth) * indent_size_;
          if (!node.children.empty()) {
            const char *symbol = expansion_->expanded(node.id) ? "v" : ">";
            tc_ui_painter_draw_text(
                context, symbol,
                tc_ui_point{toggle_x + 3.0f, cell.y + cell.height * 0.66f},
                std::max(9.0f, style.font_size - 1.0f), foreground);
          }
          text_x = toggle_x + toggle_size_;
        }
        tc_ui_painter_draw_text(
            context, node.data.cells[column].c_str(),
            tc_ui_point{text_x, cell.y + cell.height * 0.66f}, style.font_size,
            foreground);
        tc_ui_painter_pop_clip(context);
      }
    }
  }
  tc_ui_painter_pop_clip(context);
  tc_ui_painter_pop_clip(context);
  tc_ui_painter_stroke_rect(context, bounds(), style.border,
                            style.border_width);
}

size_t TreeTableWidget::row_index_at(float x, float y) const {
  if (!rect_contains(bounds(), x, y) || y < bounds().y + header_height_)
    return SIZE_MAX;
  const float local = y - bounds().y - header_height_ + scroll_y_;
  if (local < 0.0f)
    return SIZE_MAX;
  const size_t index = static_cast<size_t>(local / row_height_);
  return index < visible_.size() ? index : SIZE_MAX;
}

size_t TreeTableWidget::column_index_at(float x) const {
  const float local = x - bounds().x;
  for (size_t index = 0; index < column_layout_.size(); ++index) {
    const TableColumnLayout &column = column_layout_[index];
    if (local >= column.x && local < column.x + column.width)
      return index;
  }
  return SIZE_MAX;
}

size_t TreeTableWidget::resize_border_at(float x) const {
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

bool TreeTableWidget::point_in_toggle(const TreeVisibleRow &row,
                                      float x) const {
  if (column_layout_.empty())
    return false;
  const float left = bounds().x + column_layout_[0].x + cell_padding_ +
                     static_cast<float>(row.depth) * indent_size_;
  return x >= left && x < left + toggle_size_;
}

tc_ui_event_result
TreeTableWidget::pointer_event(tc_ui_document_handle document,
                               const tc_ui_pointer_event *event) {
  if (!event)
    return TC_UI_EVENT_IGNORED;
  sync_models();
  if (resizing_column_ != SIZE_MAX) {
    if (event->type == TC_UI_POINTER_MOVE) {
      const float width = columns_->resize(
          resizing_column_, resize_start_width_ + event->x - resize_start_x_);
      column_resized_.emit(*this, resizing_column_, width);
      return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP) {
      resizing_column_ = SIZE_MAX;
      tc_ui_document_release_pointer_capture(document, handle());
      return TC_UI_EVENT_HANDLED;
    }
  }
  if (event->type == TC_UI_POINTER_LEAVE) {
    if (hovered_ != kInvalidTreeTableNodeId) {
      hovered_ = kInvalidTreeTableNodeId;
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
    const size_t index = row_index_at(event->x, event->y);
    const TreeTableNodeId next =
        index == SIZE_MAX ? kInvalidTreeTableNodeId : visible_[index].node;
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
  const size_t index = row_index_at(event->x, event->y);
  const TreeTableNodeId node =
      index == SIZE_MAX ? kInvalidTreeTableNodeId : visible_[index].node;
  if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::RIGHT)) {
    if (node != kInvalidTreeTableNodeId)
      select_node(node, false);
    context_menu_requested_.emit(*this, node, event->x, event->y);
    return TC_UI_EVENT_HANDLED;
  }
  if (event->button != tcbase::mouse_button_value(tcbase::MouseButton::LEFT) ||
      node == kInvalidTreeTableNodeId || !model_->node(node).data.enabled) {
    activation_clicks_.clear();
    return TC_UI_EVENT_IGNORED;
  }
  if (!model_->node(node).children.empty() &&
      point_in_toggle(visible_[index], event->x)) {
    toggle(node);
    return TC_UI_EVENT_HANDLED;
  }
  const bool changed = select_node(node, false);
  const TreeTableNode &item = model_->node(node);
  if (activation_clicks_.press(item.data.stable_id, event->click_count)) {
    activated_.emit(*this, node, item.data);
    return TC_UI_EVENT_HANDLED;
  }
  (void)changed;
  return TC_UI_EVENT_HANDLED;
}

TreeTableNodeId TreeTableWidget::next_enabled(size_t from,
                                              int direction) const {
  if (visible_.empty())
    return kInvalidTreeTableNodeId;
  std::ptrdiff_t index =
      from == SIZE_MAX
          ? (direction > 0 ? 0
                           : static_cast<std::ptrdiff_t>(visible_.size()) - 1)
          : static_cast<std::ptrdiff_t>(from) + direction;
  while (index >= 0 && index < static_cast<std::ptrdiff_t>(visible_.size())) {
    const TreeTableNodeId node = visible_[static_cast<size_t>(index)].node;
    if (model_->node(node).data.enabled)
      return node;
    index += direction;
  }
  return kInvalidTreeTableNodeId;
}

TreeTableNodeId
TreeTableWidget::first_enabled_child(TreeTableNodeId node) const {
  for (TreeTableNodeId child : model_->node(node).children) {
    if (model_->node(child).data.enabled)
      return child;
  }
  return kInvalidTreeTableNodeId;
}

tc_ui_event_result TreeTableWidget::key_event(tc_ui_document_handle,
                                              const tc_ui_key_event *event) {
  if (!event || event->type != TC_UI_KEY_DOWN)
    return TC_UI_EVENT_IGNORED;
  sync_models();
  const size_t current = visible_index(selected_);
  if (event->key == TC_UI_KEY_HOME) {
    select_node(next_enabled(SIZE_MAX, 1));
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_END) {
    select_node(next_enabled(SIZE_MAX, -1));
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_UP_ARROW) {
    select_node(next_enabled(current, -1));
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_DOWN_ARROW) {
    select_node(next_enabled(current, 1));
    return TC_UI_EVENT_HANDLED;
  }
  if (!model_->contains(selected_))
    return TC_UI_EVENT_IGNORED;
  const TreeTableNode &selected = model_->node(selected_);
  if (event->key == TC_UI_KEY_RIGHT) {
    if (!selected.children.empty() && !expanded(selected_))
      set_expanded(selected_, true);
    else if (expanded(selected_))
      select_node(first_enabled_child(selected_));
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_LEFT) {
    if (!selected.children.empty() && expanded(selected_))
      set_expanded(selected_, false);
    else if (selected.parent != kInvalidTreeTableNodeId)
      select_node(selected.parent);
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_ENTER && selected.data.enabled) {
    activated_.emit(*this, selected_, selected.data);
    return TC_UI_EVENT_HANDLED;
  }
  return TC_UI_EVENT_IGNORED;
}

} // namespace termin::gui_native
