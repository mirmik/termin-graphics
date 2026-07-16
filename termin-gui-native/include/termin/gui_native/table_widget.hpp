#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/logical_click_tracker.hpp>
#include <termin/gui_native/selection_model.hpp>
#include <termin/gui_native/table_model.hpp>

namespace termin::gui_native {

struct TableColumnLayout {
    float x = 0.0f;
    float width = 0.0f;
};

class TableWidget final : public NativeWidget {
  private:
    std::shared_ptr<TableModel> model_;
    std::shared_ptr<TableColumnModel> columns_;
    SelectionModel selection_;
    std::vector<TableColumnLayout> column_layout_;
    uint64_t observed_model_revision_ = 0;
    uint64_t observed_column_revision_ = 0;
    size_t model_connection_ = 0;
    size_t column_connection_ = 0;
    float row_height_ = 26.0f;
    float header_height_ = 28.0f;
    float cell_padding_ = 6.0f;
    float scroll_y_ = 0.0f;
    float wheel_rows_ = 3.0f;
    float resize_zone_ = 4.0f;
    size_t hovered_ = SelectionModel::npos;
    LogicalClickTracker<std::string> activation_clicks_{std::string{}};
    size_t resizing_column_ = SelectionModel::npos;
    float resize_start_x_ = 0.0f;
    float resize_start_width_ = 0.0f;
    Signal<TableWidget&, const std::vector<size_t>&> selection_changed_;
    Signal<TableWidget&, size_t, TableRowId, const TableRowData&> activated_;
    Signal<TableWidget&, size_t, const TableColumn&> header_clicked_;
    Signal<TableWidget&, size_t, float> column_resized_;
    Signal<TableWidget&, int64_t, float, float> context_menu_requested_;

  public:
    explicit TableWidget(std::shared_ptr<TableModel> model = {},
                         std::shared_ptr<TableColumnModel> columns = {});
    ~TableWidget() override;

    const std::shared_ptr<TableModel>& model() const { return model_; }
    void set_model(std::shared_ptr<TableModel> model);
    const std::shared_ptr<TableColumnModel>& column_model() const { return columns_; }
    void set_column_model(std::shared_ptr<TableColumnModel> columns);
    SelectionModel& selection() { return selection_; }
    const SelectionModel& selection() const { return selection_; }
    void set_selection_mode(SelectionMode mode);

    float row_height() const { return row_height_; }
    void set_row_height(float height);
    float header_height() const { return header_height_; }
    void set_header_height(float height);
    float scroll_y() const { return scroll_y_; }
    void set_scroll_y(float offset);
    float content_height() const;
    std::pair<size_t, size_t> visible_range() const;
    void ensure_visible(size_t index);
    bool select_row(size_t index, bool toggle = false, bool extend = false, bool additive = false);
    bool clear_selection();
    const std::vector<TableColumnLayout>& column_layout() const { return column_layout_; }

    Signal<TableWidget&, const std::vector<size_t>&>& selection_changed() {
        return selection_changed_;
    }
    Signal<TableWidget&, size_t, TableRowId, const TableRowData&>& activated() {
        return activated_;
    }
    Signal<TableWidget&, size_t, const TableColumn&>& header_clicked() { return header_clicked_; }
    Signal<TableWidget&, size_t, float>& column_resized() { return column_resized_; }
    Signal<TableWidget&, int64_t, float, float>& context_menu_requested() {
        return context_menu_requested_;
    }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;

  private:
    void connect_models();
    void disconnect_models();
    void on_rows_changed(const TableChange& change);
    void on_columns_changed(const TableColumnChange& change);
    void sync_models();
    void compute_column_layout();
    void clamp_scroll();
    float body_height() const;
    size_t row_index_at(float x, float y) const;
    size_t column_index_at(float x) const;
    size_t resize_border_at(float x) const;
    size_t next_enabled(size_t from, int direction) const;
    bool apply_selection(size_t index, int32_t modifiers);
    void emit_selection_changed();

};

} // namespace termin::gui_native
