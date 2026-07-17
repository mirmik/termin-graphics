#include "widgets_test_cases.hpp"

#include <cstdlib>

using namespace termin_gui_native_test;

int main() {
  test_box_layout_sets_child_bounds_and_paints();
  test_widget_metadata_is_owned_and_exposed();
  test_dirty_flags_track_layout_paint_and_state_changes();
  test_box_layout_child_policies_allocate_primary_axis();
  test_hstack_vstack_wrappers_use_expected_orientation();
  test_grid_layout_tracks_spans_and_hit_test();
  test_grid_layout_recursive_destroy_children();
  test_group_box_lays_out_content_and_routes_hit_test();
  test_group_box_recursive_destroy_content();
  test_splitter_layout_drag_and_hit_test();
  test_splitter_recursive_destroy_children();
  test_scroll_area_lays_out_content_with_clip_and_scroll();
  test_scroll_area_can_fit_content_to_disabled_scroll_axis();
  test_scroll_area_wheel_clamps_and_recursive_destroy_content();
  test_tab_view_switches_selected_page_and_clips_paint();
  test_tab_view_recursive_destroy_pages();
  test_tab_view_page_mutation_and_selection_signal();
  test_box_layout_shrinks_flexible_children_before_overflowing();
  test_box_layout_respects_child_extent_limits();
  test_box_layout_allows_preferred_overflow_when_no_child_can_shrink();
  test_document_hit_test_returns_deepest_child();
  test_document_hit_test_prefers_topmost_root();
  test_box_layout_hit_test_skips_stale_child_handles();
  test_pointer_dispatch_updates_hovered_widget();
  test_pointer_capture_routes_events_outside_bounds_until_release();
  test_destroy_clears_hover_and_pointer_capture();
  test_focus_and_key_text_dispatch_follow_focused_widget();
  test_focus_api_rejects_non_focusable_and_clears_on_destroy();
  test_recursive_destroy_removes_container_children();
  test_controls_handle_pointer_events();
  test_separator_layout_and_paint_command();
  test_text_input_focus_text_edit_and_submit();
  test_text_widgets_clip_text_paint();
  test_text_measurement_uses_proportional_metrics();
  test_text_input_edits_utf8_at_codepoint_boundaries();
  test_text_input_scrolls_to_keep_caret_inside_clip();
  test_text_input_utf8_selection_and_host_clipboard();
  test_text_area_multiline_utf8_editing_navigation_and_scroll();
  test_spin_box_numeric_edit_buttons_and_keys();
  test_slider_edit_owns_canonical_children_and_syncs_values();
  test_combo_box_overlay_selection_and_destruction();
  test_icon_image_and_canvas_media_contracts();
  test_widget_signals_are_emitted_from_interactions();
  test_containers_register_and_replace_canonical_children();
  test_common_visibility_enabled_and_mouse_transparent_state();
  test_cpp_theme_style_facade_inheritance_and_state();
  test_collection_and_selection_models_are_reusable();
  test_list_widget_virtualizes_large_models_and_reconciles_selection();
  test_list_widget_pointer_keyboard_and_multi_selection();
  test_list_widget_model_notifications_preserve_lifetime_and_shift_selection();
  test_tree_model_stable_ids_move_and_expansion_reconcile();
  test_tree_widget_virtualizes_large_expanded_model();
  test_tree_widget_pointer_keyboard_signals_and_lifetime();
  test_tree_widget_drag_drop_positions_and_capture();
  test_file_grid_widget_virtualizes_large_model_and_responsive_layout();
  test_file_grid_widget_input_scrollbar_signals_and_lifetime();
  test_command_model_stable_ids_validation_and_mutation();
  test_tool_bar_layout_activation_capture_and_model_lifetime();
  test_status_bar_explicit_message_lifecycle_and_utf8_validation();
  test_menu_overlay_navigation_submenus_scrolling_and_dismissal();
  test_menu_bar_adjacent_switching_shortcuts_and_overlay_lifetime();
  test_dialog_modal_stack_focus_actions_and_exactly_once_results();
  test_message_box_and_input_dialog_share_modal_result_contract();
  test_table_models_preserve_row_ids_and_validate_columns();
  test_table_widget_virtualizes_large_model_and_lays_out_columns();
  test_table_widget_pointer_keyboard_resize_signals_and_lifetime();
  test_host_click_count_drives_collection_activation();
  return EXIT_SUCCESS;
}
