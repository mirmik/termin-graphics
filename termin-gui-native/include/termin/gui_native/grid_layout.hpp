#pragma once

#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class GridLayout : public NativeWidget {
public:
    explicit GridLayout(const char* debug_name = nullptr);
    GridLayout& set_padding(EdgeInsets padding);
    GridLayout& set_spacing(float column_spacing, float row_spacing);
    GridLayout& set_background(Color color);
    GridLayout& set_border(Color color, float thickness = 1.0f);
    size_t add_column(LayoutPolicy policy = LayoutPolicy::Stretch, float value = 0.0f);
    size_t add_row(LayoutPolicy policy = LayoutPolicy::Stretch, float value = 0.0f);
    void add_child(tc_widget_handle handle, size_t row, size_t column, size_t row_span = 1, size_t column_span = 1);
    void add_child(const Widget& widget, size_t row, size_t column, size_t row_span = 1, size_t column_span = 1) { add_child(widget.handle(), row, column, row_span, column_span); }
    bool set_column_extent_limits(size_t column, float min_extent, float max_extent);
    bool set_row_extent_limits(size_t row, float min_extent, float max_extent);
    const std::vector<GridTrack>& columns() const { return columns_; }
    const std::vector<GridTrack>& rows() const { return rows_; }
    const std::vector<GridItem>& items() const { return items_; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;
private:
    EdgeInsets padding_ {};
    float column_spacing_ = 0.0f;
    float row_spacing_ = 0.0f;
    Color background_ {0.0f, 0.0f, 0.0f, 0.0f};
    Color border_ {0.0f, 0.0f, 0.0f, 0.0f};
    float border_thickness_ = 0.0f;
    std::vector<GridTrack> columns_;
    std::vector<GridTrack> rows_;
    std::vector<GridItem> items_;
};
} // namespace termin::gui_native
