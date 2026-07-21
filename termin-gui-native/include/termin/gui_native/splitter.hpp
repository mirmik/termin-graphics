#pragma once

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class Splitter : public NativeWidget {
private:
    Orientation orientation_ = Orientation::Horizontal;
    float split_fraction_ = 0.5f;
    float first_min_extent_ = 32.0f;
    float second_min_extent_ = 32.0f;
    float divider_thickness_ = 4.0f;
    float divider_hit_thickness_ = 10.0f;

public:
    explicit Splitter(Orientation orientation = Orientation::Horizontal, const char* debug_name = nullptr);
    void set_first(tc_widget_handle handle);
    void set_first(const Widget& widget) { set_first(widget.handle()); }
    void set_second(tc_widget_handle handle);
    void set_second(const Widget& widget) { set_second(widget.handle()); }
    Splitter& set_split_fraction(float fraction);
    Splitter& set_min_extents(float first_min, float second_min);
    Splitter& set_divider_thickness(float thickness);
    tc_widget_handle first() const { return child_handle_at(0); }
    tc_widget_handle second() const { return child_handle_at(1); }
    float split_fraction() const { return split_fraction_; }
    float divider_thickness() const { return divider_thickness_; }
    tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document_handle document, float x, float y) override;
private:
    tc_ui_rect divider_rect() const;
    tc_ui_rect divider_hit_rect() const;
    tc_ui_rect divider_line_rect(float line_thickness) const;
    void layout_children(tc_ui_document_handle document);
    float split_axis_extent() const;
};
} // namespace termin::gui_native
