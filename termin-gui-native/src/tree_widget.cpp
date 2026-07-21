#include "widgets_internal.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

namespace {
struct WidgetLifetimeToken {
  tc_ui_document_handle document = tc_ui_document_handle_invalid();
  tc_widget_handle handle = tc_widget_handle_invalid();
};

WidgetLifetimeToken lifetime_token(const TreeWidget &widget) {
  return WidgetLifetimeToken{widget.c_widget()->document, widget.handle()};
}

bool callback_target_alive(const WidgetLifetimeToken &token) {
  return tc_ui_document_handle_is_invalid(token.document) ||
         tc_ui_document_is_alive(token.document, token.handle);
}
} // namespace

TreeWidget::TreeWidget(std::shared_ptr<TreeModel> model,
                       std::shared_ptr<TreeExpansionModel> expansion)
    : NativeWidget("TreeWidget"),
      model_(model ? std::move(model) : std::make_shared<TreeModel>()),
      expansion_(expansion ? std::move(expansion)
                           : std::make_shared<TreeExpansionModel>()) {
  set_style_role(TC_UI_STYLE_TEXT_INPUT);
  set_focusable(true);
  set_preferred_size(tc_ui_size{300.0f, 240.0f});
  connect_models();
  rebuild_visible();
}

TreeWidget::~TreeWidget() { disconnect_models(); }

void TreeWidget::connect_models() {
  if (model_ && model_connection_ == 0) {
    model_connection_ = model_->changed().connect(
        [this](TreeModel &, const TreeChange &change) {
          on_model_changed(change);
        });
  }
  if (expansion_ && expansion_connection_ == 0) {
    expansion_connection_ = expansion_->changed().connect(
        [this](TreeExpansionModel &, TreeNodeId node, bool value) {
          on_expansion_changed(node, value);
        });
  }
}

void TreeWidget::disconnect_models() {
  if (model_ && model_connection_ != 0)
    model_->changed().disconnect(model_connection_);
  if (expansion_ && expansion_connection_ != 0) {
    expansion_->changed().disconnect(expansion_connection_);
  }
  model_connection_ = 0;
  expansion_connection_ = 0;
}

void TreeWidget::set_model(std::shared_ptr<TreeModel> model) {
  disconnect_models();
  const bool selection_changed = selected_ != kInvalidTreeNodeId;
  clear_drag_state();
  model_ = model ? std::move(model) : std::make_shared<TreeModel>();
  selected_ = kInvalidTreeNodeId;
  hovered_ = kInvalidTreeNodeId;
  scroll_y_ = 0.0f;
  expansion_->reconcile(*model_);
  connect_models();
  rebuild_visible();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
  if (selection_changed)
    selection_changed_.emit(*this, selected_);
}

void TreeWidget::set_expansion_model(
    std::shared_ptr<TreeExpansionModel> expansion) {
  disconnect_models();
  clear_drag_state();
  expansion_ =
      expansion ? std::move(expansion) : std::make_shared<TreeExpansionModel>();
  expansion_->reconcile(*model_);
  connect_models();
  rebuild_visible();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
}

void TreeWidget::on_model_changed(const TreeChange &) {
  const WidgetLifetimeToken token = lifetime_token(*this);
  expansion_->reconcile(*model_);
  if (!callback_target_alive(token))
    return;
  bool selection_changed = false;
  if (selected_ != kInvalidTreeNodeId && !model_->contains(selected_)) {
    selected_ = kInvalidTreeNodeId;
    selection_changed = true;
  }
  if (hovered_ != kInvalidTreeNodeId && !model_->contains(hovered_)) {
    hovered_ = kInvalidTreeNodeId;
  }
  if (pressed_ != kInvalidTreeNodeId && !model_->contains(pressed_))
    clear_drag_state();
  rebuild_visible();
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
  if (selection_changed)
    selection_changed_.emit(*this, selected_);
}

void TreeWidget::on_expansion_changed(TreeNodeId node, bool value) {
  rebuild_visible();
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
             TC_WIDGET_DIRTY_PAINT);
  expansion_changed_.emit(*this, node, value);
}

void TreeWidget::rebuild_visible() {
  visible_ = build_tree_projection(
      model_->roots(),
      [this](TreeNodeId node) -> const std::vector<TreeNodeId> & {
        return model_->node(node).children;
      },
      [this](TreeNodeId node) { return expansion_->expanded(node); },
      model_->size());
  observed_model_revision_ = model_->revision();
  observed_expansion_revision_ = expansion_->revision();
}

void TreeWidget::sync_models() {
  if (model_->revision() != observed_model_revision_) {
    const WidgetLifetimeToken token = lifetime_token(*this);
    expansion_->reconcile(*model_);
    if (!callback_target_alive(token))
      return;
    bool selection_changed = false;
    if (selected_ != kInvalidTreeNodeId && !model_->contains(selected_)) {
      selected_ = kInvalidTreeNodeId;
      selection_changed = true;
    }
    rebuild_visible();
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE |
               TC_WIDGET_DIRTY_PAINT);
    if (selection_changed)
      selection_changed_.emit(*this, selected_);
  } else if (expansion_->revision() != observed_expansion_revision_) {
    rebuild_visible();
  }
  clamp_scroll();
}

bool TreeWidget::select_node(TreeNodeId node, bool reveal) {
  sync_models();
  if (!model_->contains(node) || !model_->node(node).item.enabled)
    return false;
  if (reveal) {
    const WidgetLifetimeToken token = lifetime_token(*this);
    ensure_visible(node);
    if (!callback_target_alive(token))
      return false;
  }
  if (selected_ == node)
    return false;
  selected_ = node;
  mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
  selection_changed_.emit(*this, selected_);
  return true;
}

bool TreeWidget::clear_selection() {
  if (selected_ == kInvalidTreeNodeId)
    return false;
  selected_ = kInvalidTreeNodeId;
  mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
  selection_changed_.emit(*this, selected_);
  return true;
}

bool TreeWidget::set_expanded(TreeNodeId node, bool value) {
  if (!model_->contains(node) || model_->node(node).children.empty())
    return false;
  return expansion_->set_expanded(node, value);
}

bool TreeWidget::toggle(TreeNodeId node) {
  return set_expanded(node, !expansion_->expanded(node));
}

void TreeWidget::set_row_height(float height) {
  if (!std::isfinite(height) || height <= 0.0f) {
    tc_log_error("[termin-gui-native] TreeWidget rejected invalid row height");
    throw std::invalid_argument("row height must be finite and positive");
  }
  row_height_ = height;
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TreeWidget::set_row_spacing(float spacing) {
  if (!std::isfinite(spacing) || spacing < 0.0f) {
    tc_log_error("[termin-gui-native] TreeWidget rejected invalid row spacing");
    throw std::invalid_argument("row spacing must be finite and non-negative");
  }
  row_spacing_ = spacing;
  clamp_scroll();
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TreeWidget::set_indent_size(float size) {
  if (!std::isfinite(size) || size < 0.0f) {
    tc_log_error("[termin-gui-native] TreeWidget rejected invalid indent size");
    throw std::invalid_argument("indent size must be finite and non-negative");
  }
  indent_size_ = size;
  mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

float TreeWidget::content_height() const {
  if (visible_.empty())
    return 0.0f;
  return static_cast<float>(visible_.size()) * row_height_ +
         static_cast<float>(visible_.size() - 1) * row_spacing_;
}

void TreeWidget::clamp_scroll() {
  scroll_y_ = clamp_float(scroll_y_, 0.0f,
                          std::max(0.0f, content_height() - bounds().height));
}

void TreeWidget::set_scroll_y(float offset) {
  if (!std::isfinite(offset)) {
    tc_log_error(
        "[termin-gui-native] TreeWidget rejected non-finite scroll offset");
    throw std::invalid_argument("scroll offset must be finite");
  }
  sync_models();
  const float before = scroll_y_;
  scroll_y_ = offset;
  clamp_scroll();
  if (before != scroll_y_)
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

size_t TreeWidget::visible_count() {
  sync_models();
  return visible_.size();
}

std::pair<size_t, size_t> TreeWidget::visible_range() {
  sync_models();
  if (visible_.empty() || bounds().height <= 0.0f)
    return {0, 0};
  const float stride = row_height_ + row_spacing_;
  const size_t first = std::min(
      visible_.size(),
      static_cast<size_t>(std::max(0.0f, std::floor(scroll_y_ / stride))));
  const size_t last = std::min(
      visible_.size(),
      static_cast<size_t>(std::ceil((scroll_y_ + bounds().height) / stride)) +
          1);
  return {first, last};
}

TreeVisibleRow TreeWidget::visible_row(size_t index) {
  sync_models();
  if (index >= visible_.size())
    throw std::out_of_range("visible tree row index out of range");
  return visible_[index];
}

size_t TreeWidget::visible_index(TreeNodeId node) const {
  for (size_t index = 0; index < visible_.size(); ++index) {
    if (visible_[index].node == node)
      return index;
  }
  return SIZE_MAX;
}

void TreeWidget::ensure_visible(TreeNodeId node) {
  if (!model_->contains(node))
    return;
  std::vector<TreeNodeId> ancestors;
  TreeNodeId parent = model_->node(node).parent;
  while (parent != kInvalidTreeNodeId) {
    ancestors.push_back(parent);
    parent = model_->node(parent).parent;
  }
  for (TreeNodeId ancestor : ancestors) {
    if (!model_->node(ancestor).children.empty()) {
      const WidgetLifetimeToken token = lifetime_token(*this);
      expansion_->set_expanded(ancestor, true);
      if (!callback_target_alive(token))
        return;
    }
  }
  sync_models();
  const size_t index = visible_index(node);
  if (index == SIZE_MAX)
    return;
  const float stride = row_height_ + row_spacing_;
  const float top = static_cast<float>(index) * stride;
  const float bottom = top + row_height_;
  if (top < scroll_y_)
    set_scroll_y(top);
  else if (bottom > scroll_y_ + bounds().height)
    set_scroll_y(bottom - bounds().height);
}

tc_ui_size TreeWidget::measure(tc_ui_document_handle document,
                               tc_ui_constraints constraints) {
  sync_models();
  const tc_ui_style style = computed_style(document);
  return clamp_size(
      tc_ui_size{std::max(preferred_size().width, style.min_width),
                 std::max(preferred_size().height, style.min_height)},
      constraints);
}

void TreeWidget::layout(tc_ui_document_handle document, tc_ui_rect rect) {
  sync_models();
  NativeWidget::layout(document, rect);
  clamp_scroll();
}

void TreeWidget::paint(tc_ui_document_handle document, tc_ui_paint_context *context) {
  sync_models();
  const tc_ui_style style = computed_style(document);
  tc_ui_painter_fill_rect(context, bounds(), style.background);
  tc_ui_painter_push_clip(context, bounds());
  if (visible_.empty()) {
    tc_ui_color muted = style.foreground;
    muted.a *= 0.6f;
    tc_ui_painter_draw_text(context, "No items",
                            tc_ui_point{bounds().x + item_padding_,
                                        bounds().y + row_height_ * 0.68f},
                            style.font_size, muted);
  } else {
    const auto [first, last] = visible_range();
    const float stride = row_height_ + row_spacing_;
    for (size_t index = first; index < last; ++index) {
      const TreeVisibleRow row_info = visible_[index];
      const TreeNode &node = model_->node(row_info.node);
      const float y =
          bounds().y + static_cast<float>(index) * stride - scroll_y_;
      const tc_ui_rect row{bounds().x, y, bounds().width, row_height_};
      if (selected_ == node.id || hovered_ == node.id) {
        tc_ui_color highlight = style.accent;
        highlight.a *= selected_ == node.id ? 0.42f : 0.20f;
        tc_ui_painter_fill_rounded_rect(
            context, row, std::min(4.0f, row_height_ * 0.2f), highlight);
      }
      if (dragging_ && drag_target_ == node.id) {
        tc_ui_color drop_color = style.accent;
        drop_color.a = 0.9f;
        if (drag_position_ == TreeDropPosition::Inside) {
          tc_ui_color fill = drop_color;
          fill.a = 0.28f;
          tc_ui_painter_fill_rounded_rect(
              context, row, std::min(4.0f, row_height_ * 0.2f), fill);
        } else {
          const float line_y = drag_position_ == TreeDropPosition::Before
                                   ? row.y + 1.0f
                                   : row.y + row.height - 1.0f;
          tc_ui_painter_draw_line(context, tc_ui_point{row.x, line_y},
                                  tc_ui_point{row.x + row.width, line_y},
                                  drop_color, 2.0f);
        }
      }
      tc_ui_color foreground = style.foreground;
      if (!node.item.enabled)
        foreground.a *= 0.45f;
      const float toggle_x =
          row.x + static_cast<float>(row_info.depth) * indent_size_;
      if (!node.children.empty()) {
        const char *symbol = expansion_->expanded(node.id) ? "v" : ">";
        tc_ui_painter_draw_text(
            context, symbol,
            tc_ui_point{toggle_x + 3.0f, row.y + row.height * 0.66f},
            std::max(9.0f, style.font_size - 1.0f), foreground);
      }
      const float text_x = toggle_x + toggle_size_ + item_padding_;
      tc_ui_painter_draw_text(context, node.item.text.c_str(),
                              tc_ui_point{text_x, row.y + row.height * 0.66f},
                              style.font_size, foreground);
    }
  }
  tc_ui_painter_pop_clip(context);
  if (dragging_ && drag_position_ == TreeDropPosition::Root) {
    tc_ui_color drop_color = style.accent;
    drop_color.a = 0.9f;
    const float line_y = bounds().y + bounds().height - 2.0f;
    tc_ui_painter_draw_line(context, tc_ui_point{bounds().x, line_y},
                            tc_ui_point{bounds().x + bounds().width, line_y},
                            drop_color, 2.0f);
  }
  tc_ui_painter_stroke_rect(context, bounds(), style.border,
                            style.border_width);
}

size_t TreeWidget::row_index_at(float x, float y) const {
  if (!rect_contains(bounds(), x, y))
    return SIZE_MAX;
  const float stride = row_height_ + row_spacing_;
  const float local = y - bounds().y + scroll_y_;
  if (local < 0.0f)
    return SIZE_MAX;
  const size_t index = static_cast<size_t>(local / stride);
  if (index >= visible_.size() ||
      local - static_cast<float>(index) * stride > row_height_) {
    return SIZE_MAX;
  }
  return index;
}

bool TreeWidget::point_in_toggle(const TreeVisibleRow &row, float x) const {
  const float left = bounds().x + static_cast<float>(row.depth) * indent_size_;
  return x >= left && x < left + toggle_size_;
}

TreeDropPosition TreeWidget::drop_position_at(size_t index, float y) const {
  if (index == SIZE_MAX)
    return TreeDropPosition::Root;
  const float stride = row_height_ + row_spacing_;
  const float row_y =
      bounds().y + static_cast<float>(index) * stride - scroll_y_;
  const float fraction = clamp_float((y - row_y) / row_height_, 0.0f, 1.0f);
  if (fraction < 0.25f)
    return TreeDropPosition::Before;
  if (fraction > 0.75f)
    return TreeDropPosition::After;
  return TreeDropPosition::Inside;
}

void TreeWidget::clear_drag_state() {
  pressed_ = kInvalidTreeNodeId;
  drag_target_ = kInvalidTreeNodeId;
  drag_position_ = TreeDropPosition::Root;
  dragging_ = false;
  mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_event_result TreeWidget::pointer_event(tc_ui_document_handle document,
                                             const tc_ui_pointer_event *event) {
  if (!event)
    return TC_UI_EVENT_IGNORED;
  sync_models();
  if (event->type == TC_UI_POINTER_LEAVE) {
    if (hovered_ != kInvalidTreeNodeId) {
      hovered_ = kInvalidTreeNodeId;
      mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }
    return TC_UI_EVENT_IGNORED;
  }
  const bool captured =
      tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
  if (event->type == TC_UI_POINTER_MOVE && pressed_ != kInvalidTreeNodeId &&
      captured) {
    const float dx = event->x - press_x_;
    const float dy = event->y - press_y_;
    if (!dragging_ && draggable_ && dx * dx + dy * dy >= 16.0f)
      dragging_ = true;
    if (dragging_) {
      const size_t index = row_index_at(event->x, event->y);
      drag_target_ =
          index == SIZE_MAX ? kInvalidTreeNodeId : visible_[index].node;
      drag_position_ = drop_position_at(index, event->y);
      mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
      return TC_UI_EVENT_HANDLED;
    }
  }
  if (event->type == TC_UI_POINTER_UP && pressed_ != kInvalidTreeNodeId &&
      captured) {
    const TreeNodeId dragged = pressed_;
    const TreeNodeId target = drag_target_;
    const TreeDropPosition position = drag_position_;
    const bool was_dragging = dragging_;
    tc_ui_document_release_pointer_capture(document, handle());
    clear_drag_state();
    if (was_dragging)
      drop_requested_.emit(*this, dragged, target, position);
    return TC_UI_EVENT_HANDLED;
  }
  if (!rect_contains(bounds(), event->x, event->y))
    return TC_UI_EVENT_IGNORED;
  if (event->type == TC_UI_POINTER_WHEEL) {
    const float before = scroll_y_;
    set_scroll_y(scroll_y_ - event->wheel_y * row_height_ * wheel_rows_);
    return before != scroll_y_ ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
  }
  const size_t index = row_index_at(event->x, event->y);
  if (event->type == TC_UI_POINTER_MOVE) {
    const TreeNodeId next =
        index == SIZE_MAX ? kInvalidTreeNodeId : visible_[index].node;
    if (next != hovered_) {
      hovered_ = next;
      mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }
    return TC_UI_EVENT_HANDLED;
  }
  if (event->type != TC_UI_POINTER_DOWN)
    return TC_UI_EVENT_IGNORED;
  if (index == SIZE_MAX) {
    if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT))
      activation_clicks_.clear();
    return TC_UI_EVENT_IGNORED;
  }
  tc_ui_document_set_focus(document, handle());
  const TreeVisibleRow row = visible_[index];
  const TreeNode &node = model_->node(row.node);
  if (!node.children.empty() && point_in_toggle(row, event->x)) {
    if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT))
      activation_clicks_.clear();
    toggle(node.id);
    return TC_UI_EVENT_HANDLED;
  }
  if (!node.item.enabled) {
    if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT))
      activation_clicks_.clear();
    return TC_UI_EVENT_IGNORED;
  }
  const bool selected = select_node(node.id, false) || selected_ == node.id;
  if (event->button == tcbase::mouse_button_value(tcbase::MouseButton::RIGHT)) {
    context_menu_requested_.emit(*this, node.id, event->x, event->y);
    return TC_UI_EVENT_HANDLED;
  }
  if (event->button != tcbase::mouse_button_value(tcbase::MouseButton::LEFT))
    return TC_UI_EVENT_IGNORED;
  if (draggable_) {
    pressed_ = node.id;
    press_x_ = event->x;
    press_y_ = event->y;
    tc_ui_document_set_pointer_capture(document, handle());
  }
  if (activation_clicks_.press(node.item.stable_id, event->click_count)) {
    activated_.emit(*this, node.id, node.item);
    return TC_UI_EVENT_HANDLED;
  }
  return selected ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
}

TreeNodeId TreeWidget::next_enabled(size_t from, int direction) const {
  if (visible_.empty())
    return kInvalidTreeNodeId;
  std::ptrdiff_t index =
      from == SIZE_MAX
          ? (direction > 0 ? 0
                           : static_cast<std::ptrdiff_t>(visible_.size()) - 1)
          : static_cast<std::ptrdiff_t>(from) + direction;
  while (index >= 0 && index < static_cast<std::ptrdiff_t>(visible_.size())) {
    const TreeNodeId node = visible_[static_cast<size_t>(index)].node;
    if (model_->node(node).item.enabled)
      return node;
    index += direction;
  }
  return kInvalidTreeNodeId;
}

TreeNodeId TreeWidget::first_enabled_child(TreeNodeId node) const {
  for (TreeNodeId child : model_->node(node).children) {
    if (model_->node(child).item.enabled)
      return child;
  }
  return kInvalidTreeNodeId;
}

bool TreeWidget::select_from_navigation(TreeNodeId node) {
  if (node == kInvalidTreeNodeId)
    return true;
  select_node(node, true);
  return true;
}

tc_ui_event_result TreeWidget::key_event(tc_ui_document_handle,
                                         const tc_ui_key_event *event) {
  if (!event || event->type != TC_UI_KEY_DOWN)
    return TC_UI_EVENT_IGNORED;
  sync_models();
  const size_t current = visible_index(selected_);
  if (event->key == TC_UI_KEY_HOME) {
    select_from_navigation(next_enabled(SIZE_MAX, 1));
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_END) {
    select_from_navigation(next_enabled(SIZE_MAX, -1));
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_UP_ARROW) {
    select_from_navigation(next_enabled(current, -1));
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_DOWN_ARROW) {
    select_from_navigation(next_enabled(current, 1));
    return TC_UI_EVENT_HANDLED;
  }
  if (!model_->contains(selected_))
    return TC_UI_EVENT_IGNORED;
  const TreeNode &selected = model_->node(selected_);
  if (event->key == TC_UI_KEY_RIGHT) {
    if (!selected.children.empty() && !expansion_->expanded(selected_)) {
      set_expanded(selected_, true);
    } else if (expansion_->expanded(selected_)) {
      select_from_navigation(first_enabled_child(selected_));
    }
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_LEFT) {
    if (!selected.children.empty() && expansion_->expanded(selected_)) {
      set_expanded(selected_, false);
    } else if (selected.parent != kInvalidTreeNodeId) {
      select_from_navigation(selected.parent);
    }
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_ENTER) {
    if (selected.item.enabled)
      activated_.emit(*this, selected_, selected.item);
    return TC_UI_EVENT_HANDLED;
  }
  if (event->key == TC_UI_KEY_DELETE && selected.item.enabled) {
    delete_requested_.emit(*this, selected_, selected.item);
    return TC_UI_EVENT_HANDLED;
  }
  return TC_UI_EVENT_IGNORED;
}

} // namespace termin::gui_native
