#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/logical_click_tracker.hpp>
#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/table_widget.hpp>
#include <termin/gui_native/tree_table_model.hpp>
#include <termin/gui_native/tree_widget.hpp>

namespace termin::gui_native {

class TreeTableWidget final : public NativeWidget {
private:
  std::shared_ptr<TreeTableModel> model_;
  std::shared_ptr<TableColumnModel> columns_;
  std::shared_ptr<TreeExpansionModel> expansion_;
  std::vector<TreeVisibleRow> visible_;
  std::vector<TableColumnLayout> column_layout_;
  uint64_t observed_model_revision_ = 0;
  uint64_t observed_column_revision_ = 0;
  uint64_t observed_expansion_revision_ = 0;
  size_t model_connection_ = 0;
  size_t column_connection_ = 0;
  size_t expansion_connection_ = 0;
  TreeTableNodeId selected_ = kInvalidTreeTableNodeId;
  TreeTableNodeId hovered_ = kInvalidTreeTableNodeId;
  LogicalClickTracker<std::string> activation_clicks_{std::string{}};
  float row_height_ = 26.0f;
  float header_height_ = 28.0f;
  float cell_padding_ = 6.0f;
  float indent_size_ = 16.0f;
  float toggle_size_ = 16.0f;
  float scroll_y_ = 0.0f;
  float wheel_rows_ = 3.0f;
  float resize_zone_ = 4.0f;
  size_t resizing_column_ = SIZE_MAX;
  float resize_start_x_ = 0.0f;
  float resize_start_width_ = 0.0f;
  Signal<TreeTableWidget &, TreeTableNodeId> selection_changed_;
  Signal<TreeTableWidget &, TreeTableNodeId, bool> expansion_changed_;
  Signal<TreeTableWidget &, TreeTableNodeId, const TreeTableRowData &>
      activated_;
  Signal<TreeTableWidget &, size_t, const TableColumn &> header_clicked_;
  Signal<TreeTableWidget &, size_t, float> column_resized_;
  Signal<TreeTableWidget &, TreeTableNodeId, float, float>
      context_menu_requested_;

public:
  explicit TreeTableWidget(std::shared_ptr<TreeTableModel> model = {},
                           std::shared_ptr<TableColumnModel> columns = {},
                           std::shared_ptr<TreeExpansionModel> expansion = {});
  ~TreeTableWidget() override;

  const std::shared_ptr<TreeTableModel> &model() const { return model_; }
  void set_model(std::shared_ptr<TreeTableModel> model);
  const std::shared_ptr<TableColumnModel> &column_model() const {
    return columns_;
  }
  void set_column_model(std::shared_ptr<TableColumnModel> columns);
  const std::shared_ptr<TreeExpansionModel> &expansion_model() const {
    return expansion_;
  }
  void set_expansion_model(std::shared_ptr<TreeExpansionModel> expansion);

  TreeTableNodeId selected_node() const { return selected_; }
  bool select_node(TreeTableNodeId node, bool reveal = true);
  bool clear_selection();
  bool set_expanded(TreeTableNodeId node, bool expanded);
  bool toggle(TreeTableNodeId node);
  bool expanded(TreeTableNodeId node) const {
    return expansion_->expanded(node);
  }

  float row_height() const { return row_height_; }
  void set_row_height(float height);
  float header_height() const { return header_height_; }
  void set_header_height(float height);
  float indent_size() const { return indent_size_; }
  void set_indent_size(float size);
  float scroll_y() const { return scroll_y_; }
  void set_scroll_y(float offset);
  float content_height() const;
  size_t visible_count();
  std::pair<size_t, size_t> visible_range();
  TreeVisibleRow visible_row(size_t index);
  void ensure_visible(TreeTableNodeId node);
  const std::vector<TableColumnLayout> &column_layout() const {
    return column_layout_;
  }

  Signal<TreeTableWidget &, TreeTableNodeId> &selection_changed() {
    return selection_changed_;
  }
  Signal<TreeTableWidget &, TreeTableNodeId, bool> &expansion_changed() {
    return expansion_changed_;
  }
  Signal<TreeTableWidget &, TreeTableNodeId, const TreeTableRowData &> &
  activated() {
    return activated_;
  }
  Signal<TreeTableWidget &, size_t, const TableColumn &> &header_clicked() {
    return header_clicked_;
  }
  Signal<TreeTableWidget &, size_t, float> &column_resized() {
    return column_resized_;
  }
  Signal<TreeTableWidget &, TreeTableNodeId, float, float> &
  context_menu_requested() {
    return context_menu_requested_;
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
  void on_model_changed();
  void on_columns_changed();
  void on_expansion_changed(TreeTableNodeId node, bool expanded);
  void sync_models();
  void rebuild_visible();
  void compute_column_layout();
  void clamp_scroll();
  float body_height() const;
  size_t row_index_at(float x, float y) const;
  size_t column_index_at(float x) const;
  size_t resize_border_at(float x) const;
  size_t visible_index(TreeTableNodeId node) const;
  bool point_in_toggle(const TreeVisibleRow &row, float x) const;
  TreeTableNodeId next_enabled(size_t from, int direction) const;
  TreeTableNodeId first_enabled_child(TreeTableNodeId node) const;
};

} // namespace termin::gui_native
