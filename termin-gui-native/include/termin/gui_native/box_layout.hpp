#pragma once

#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {

class BoxLayout : public NativeWidget {
private:
    Orientation orientation_;
    EdgeInsets padding_ {};
    float spacing_ = 0.0f;
    Color background_ {0.0f, 0.0f, 0.0f, 0.0f};
    Color border_ {0.0f, 0.0f, 0.0f, 0.0f};
    float border_thickness_ = 0.0f;
    float corner_radius_ = 0.0f;
    std::vector<LayoutItem> items_;

public:
    explicit BoxLayout(Orientation orientation, const char* debug_name = nullptr);
    BoxLayout& set_padding(EdgeInsets padding);
    BoxLayout& set_spacing(float spacing);
    BoxLayout& set_background(Color color);
    BoxLayout& set_border(Color color, float thickness = 1.0f);
    BoxLayout& set_corner_radius(float radius);
    void add_child(tc_widget_handle handle);
    void add_child(tc_widget_handle handle, LayoutPolicy policy, float value = 0.0f);
    void add_child(const Widget& widget) { add_child(widget.handle()); }
    void add_child(const Widget& widget, LayoutPolicy policy, float value = 0.0f) { add_child(widget.handle(), policy, value); }
    void add_fixed_child(tc_widget_handle handle, float extent);
    void add_fixed_child(const Widget& widget, float extent) { add_fixed_child(widget.handle(), extent); }
    void add_preferred_child(tc_widget_handle handle);
    void add_preferred_child(const Widget& widget) { add_preferred_child(widget.handle()); }
    void add_flex_child(tc_widget_handle handle, float flex = 1.0f);
    void add_flex_child(const Widget& widget, float flex = 1.0f) { add_flex_child(widget.handle(), flex); }
    void add_stretch_child(tc_widget_handle handle);
    void add_stretch_child(const Widget& widget) { add_stretch_child(widget.handle()); }
    bool set_child_extent_limits(tc_widget_handle handle, float min_extent, float max_extent);
    bool set_child_extent_limits(const Widget& widget, float min_extent, float max_extent) { return set_child_extent_limits(widget.handle(), min_extent, max_extent); }
    const std::vector<LayoutItem>& items() const { return items_; }
    std::vector<tc_widget_handle> children() const;
    tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document_handle document, float x, float y) override;
};

} // namespace termin::gui_native
