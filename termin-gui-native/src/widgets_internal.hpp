#pragma once

#include <termin/gui_native/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>
#include <tcbase/tc_log.h>

namespace termin::gui_native::detail {

extern const float kHuge;

float clamp_float(float value, float lo, float hi);
bool color_visible(Color color);
bool color_visible(tc_ui_color color);
void set_style_color(Widget& widget, tc_ui_style_field_mask field, tc_ui_color color);
void set_style_metric(Widget& widget, tc_ui_style_field_mask field, float value);
bool rect_contains(tc_ui_rect rect, float x, float y);
bool command_modifier(int32_t modifiers);
bool key_matches_ascii(int32_t key, char lower);
bool decode_utf8(std::string_view text, size_t& offset, uint32_t& codepoint);
bool valid_utf8(std::string_view text);
size_t utf8_floor_boundary(std::string_view text, size_t offset);
size_t utf8_previous_boundary(std::string_view text, size_t offset);
size_t utf8_next_boundary(std::string_view text, size_t offset);
bool measure_text(tc_ui_document* document, std::string_view text, float font_size, tc_ui_text_metrics& metrics);
float centered_text_baseline(tc_ui_document* document, std::string_view text, float font_size, tc_ui_rect rect);
tc_ui_size clamp_size(tc_ui_size size, tc_ui_constraints constraints);
float effective_max(float value);
tc_ui_constraints unconstrained();
tc_widget* resolve_child(tc_ui_document* document, const tc_widget* expected_parent, tc_widget_handle handle, const char* owner);
tc_widget* attach_child(tc_widget* parent, tc_widget_handle child_handle, size_t index, const char* owner);
void detach_if_child(tc_widget* parent, tc_widget_handle child_handle);
tc_ui_size measure_widget(tc_widget* widget, tc_ui_document* document, tc_ui_constraints constraints);
NativeWidget* native_widget_body(tc_widget* widget);
void layout_widget(tc_widget* widget, tc_ui_document* document, tc_ui_rect rect);
void paint_widget(tc_widget* widget, tc_ui_document* document, tc_ui_paint_context* context);
tc_ui_rect inset_rect(tc_ui_rect rect, EdgeInsets padding);
float primary_size(tc_ui_size size, Orientation orientation);
float cross_size(tc_ui_size size, Orientation orientation);
float item_basis(const LayoutItem& item, tc_ui_size measured, Orientation orientation);
float item_min_extent(const LayoutItem& item, const NativeWidget* native, Orientation orientation);
float item_max_extent(const LayoutItem& item, const NativeWidget* native, Orientation orientation);
void distribute_grow(std::vector<float>& extents, const std::vector<float>& max_extents, const std::vector<float>& weights, float extra);
void distribute_shrink(std::vector<float>& extents, const std::vector<float>& min_extents, const std::vector<float>& weights, float deficit);
GridTrack make_grid_track(LayoutPolicy policy, float value);
void set_track_limits(GridTrack& track, float min_extent, float max_extent);
const LayoutItem* find_layout_item(const std::vector<LayoutItem>& items, tc_widget_handle handle);
const TabPage* find_tab_page(const std::vector<TabPage>& pages, tc_widget_handle handle);
float track_base_extent(const GridTrack& track);
float track_max_extent(const GridTrack& track);
void apply_span_requirement(std::vector<float>& extents, const std::vector<float>& max_extents, size_t start, size_t span, float spacing, float required_extent);

struct GridAxisLayout {
    std::vector<float> extents;
    std::vector<float> min_extents;
    std::vector<float> max_extents;
    std::vector<float> grow_weights;
    std::vector<float> shrink_weights;
};

GridAxisLayout build_grid_axis(tc_ui_document* document, const tc_widget* expected_parent, const std::vector<GridTrack>& tracks, const std::vector<GridItem>& items, bool columns, float spacing);
float axis_total_extent(const std::vector<float>& extents, float spacing);

} // namespace termin::gui_native::detail
