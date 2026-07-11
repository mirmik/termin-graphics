#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <termin/gui_native/menu.hpp>

namespace termin::gui_native {

struct MenuBarEntry {
    std::string stable_id;
    std::string label;
    std::shared_ptr<CommandModel> menu;
};

class MenuBar final : public NativeWidget {
  private:
    std::vector<MenuBarEntry> entries_;
    std::vector<tc_ui_rect> item_rects_;
    tc_widget_handle popup_handle_ = tc_widget_handle_invalid();
    size_t popup_activated_connection_ = 0;
    size_t popup_dismissed_connection_ = 0;
    size_t popup_adjacent_connection_ = 0;
    float item_height_ = 30.0f;
    float padding_x_ = 11.0f;
    size_t hovered_ = SIZE_MAX;
    size_t open_index_ = SIZE_MAX;
    Signal<MenuBar&, size_t, CommandId, const CommandData&> activated_;

  public:
    MenuBar();

    const std::vector<MenuBarEntry>& entries() const { return entries_; }
    void set_entries(std::vector<MenuBarEntry> entries);
    void add_menu(MenuBarEntry entry);
    void clear();
    bool menu_open() const { return open_index_ != SIZE_MAX; }
    size_t open_index() const { return open_index_; }
    const std::vector<tc_ui_rect>& item_rects() const { return item_rects_; }
    bool dispatch_shortcut(int32_t key, int32_t modifiers);

    Signal<MenuBar&, size_t, CommandId, const CommandData&>& activated() { return activated_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    void on_destroy(tc_ui_document* document) override;

  private:
    static void validate_entries(const std::vector<MenuBarEntry>& entries);
    void compute_item_rects(tc_ui_document* document);
    size_t index_at(float x, float y) const;
    bool open_menu(tc_ui_document* document, size_t index, bool select_first = false);
    void close_menu(tc_ui_document* document);
    void switch_menu(tc_ui_document* document, int direction);
    bool dispatch_shortcut_in(const std::shared_ptr<CommandModel>& model, int32_t key,
                              int32_t modifiers, std::unordered_set<const CommandModel*>& visited,
                              size_t menu_index);

};

} // namespace termin::gui_native
