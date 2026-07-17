#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/logical_click_tracker.hpp>
#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/tree_model.hpp>
#include <termin/gui_native/tree_projection.hpp>

namespace termin::gui_native {

enum class TreeDropPosition { Before, Inside, After, Root };

class TreeWidget final : public NativeWidget {
private:
  std::shared_ptr<TreeModel> model_;
  std::shared_ptr<TreeExpansionModel> expansion_;
  std::vector<TreeVisibleRow> visible_;
  uint64_t observed_model_revision_ = 0;
  uint64_t observed_expansion_revision_ = 0;
  size_t model_connection_ = 0;
  size_t expansion_connection_ = 0;
  TreeNodeId selected_ = kInvalidTreeNodeId;
  TreeNodeId hovered_ = kInvalidTreeNodeId;
  TreeNodeId pressed_ = kInvalidTreeNodeId;
  LogicalClickTracker<std::string> activation_clicks_{std::string{}};
  TreeNodeId drag_target_ = kInvalidTreeNodeId;
  TreeDropPosition drag_position_ = TreeDropPosition::Root;
  float press_x_ = 0.0f;
  float press_y_ = 0.0f;
  bool draggable_ = false;
  bool dragging_ = false;
  float row_height_ = 28.0f;
  float row_spacing_ = 0.0f;
  float indent_size_ = 16.0f;
  float toggle_size_ = 16.0f;
  float item_padding_ = 4.0f;
  float scroll_y_ = 0.0f;
  float wheel_rows_ = 3.0f;
  Signal<TreeWidget &, TreeNodeId> selection_changed_;
  Signal<TreeWidget &, TreeNodeId, bool> expansion_changed_;
  Signal<TreeWidget &, TreeNodeId, const CollectionItem &> activated_;
  Signal<TreeWidget &, TreeNodeId, const CollectionItem &> delete_requested_;
  Signal<TreeWidget &, TreeNodeId, float, float> context_menu_requested_;
  Signal<TreeWidget &, TreeNodeId, TreeNodeId, TreeDropPosition>
      drop_requested_;

public:
  explicit TreeWidget(std::shared_ptr<TreeModel> model = {},
                      std::shared_ptr<TreeExpansionModel> expansion = {});
  ~TreeWidget() override;

  const std::shared_ptr<TreeModel> &model() const { return model_; }
  void set_model(std::shared_ptr<TreeModel> model);
  const std::shared_ptr<TreeExpansionModel> &expansion_model() const {
    return expansion_;
  }
  void set_expansion_model(std::shared_ptr<TreeExpansionModel> expansion);

  TreeNodeId selected_node() const { return selected_; }
  bool select_node(TreeNodeId node, bool reveal = true);
  bool clear_selection();
  bool set_expanded(TreeNodeId node, bool expanded);
  bool toggle(TreeNodeId node);
  bool expanded(TreeNodeId node) const { return expansion_->expanded(node); }

  float row_height() const { return row_height_; }
  void set_row_height(float height);
  float row_spacing() const { return row_spacing_; }
  void set_row_spacing(float spacing);
  float indent_size() const { return indent_size_; }
  void set_indent_size(float size);
  float scroll_y() const { return scroll_y_; }
  void set_scroll_y(float offset);
  float content_height() const;
  size_t visible_count();
  std::pair<size_t, size_t> visible_range();
  TreeVisibleRow visible_row(size_t index);
  void ensure_visible(TreeNodeId node);
  bool draggable() const { return draggable_; }
  void set_draggable(bool value) { draggable_ = value; }
  bool dragging() const { return dragging_; }

  Signal<TreeWidget &, TreeNodeId> &selection_changed() {
    return selection_changed_;
  }
  Signal<TreeWidget &, TreeNodeId, bool> &expansion_changed() {
    return expansion_changed_;
  }
  Signal<TreeWidget &, TreeNodeId, const CollectionItem &> &activated() {
    return activated_;
  }
  Signal<TreeWidget &, TreeNodeId, const CollectionItem &> &delete_requested() {
    return delete_requested_;
  }
  Signal<TreeWidget &, TreeNodeId, float, float> &context_menu_requested() {
    return context_menu_requested_;
  }
  Signal<TreeWidget &, TreeNodeId, TreeNodeId, TreeDropPosition> &
  drop_requested() {
    return drop_requested_;
  }

  tc_ui_size measure(tc_ui_document *document,
                     tc_ui_constraints constraints) override;
  void layout(tc_ui_document *document, tc_ui_rect rect) override;
  void paint(tc_ui_document *document, tc_ui_paint_context *context) override;
  tc_ui_event_result pointer_event(tc_ui_document *document,
                                   const tc_ui_pointer_event *event) override;
  tc_ui_event_result key_event(tc_ui_document *document,
                               const tc_ui_key_event *event) override;

private:
  void connect_models();
  void disconnect_models();
  void on_model_changed(const TreeChange &change);
  void on_expansion_changed(TreeNodeId node, bool expanded);
  void rebuild_visible();
  void sync_models();
  void clamp_scroll();
  size_t row_index_at(float x, float y) const;
  size_t visible_index(TreeNodeId node) const;
  TreeNodeId next_enabled(size_t from, int direction) const;
  TreeNodeId first_enabled_child(TreeNodeId node) const;
  bool select_from_navigation(TreeNodeId node);
  bool point_in_toggle(const TreeVisibleRow &row, float x) const;
  TreeDropPosition drop_position_at(size_t index, float y) const;
  void clear_drag_state();
};

} // namespace termin::gui_native
