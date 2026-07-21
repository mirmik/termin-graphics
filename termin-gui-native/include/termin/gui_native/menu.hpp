#pragma once

#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <termin/gui_native/command_model.hpp>
#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {

class Menu final : public NativeWidget {
  private:
    std::shared_ptr<CommandModel> model_;
    uint64_t observed_revision_ = 0;
    size_t model_connection_ = 0;
    std::vector<float> item_tops_;
    Menu* parent_menu_ = nullptr;
    tc_widget_handle child_handle_ = tc_widget_handle_invalid();
    size_t child_index_ = SIZE_MAX;
    tc_widget_handle anchor_owner_ = tc_widget_handle_invalid();
    std::unordered_set<const CommandModel*> ancestors_;
    tc_ui_rect viewport_{};
    float item_height_ = 28.0f;
    float separator_height_ = 9.0f;
    float padding_x_ = 10.0f;
    float padding_y_ = 5.0f;
    float column_gap_ = 18.0f;
    float min_width_ = 150.0f;
    float max_visible_height_ = 420.0f;
    float content_height_ = 0.0f;
    float scroll_offset_ = 0.0f;
    size_t current_ = SIZE_MAX;
    bool open_ = false;
    Signal<Menu&, size_t, CommandId, const CommandData&> activated_;
    Signal<Menu&, tc_ui_overlay_dismiss_reason> dismissed_;
    Signal<Menu&, int> adjacent_requested_;

  public:
    explicit Menu(std::shared_ptr<CommandModel> model = {});
    ~Menu() override;

    const std::shared_ptr<CommandModel>& model() const { return model_; }
    void set_model(std::shared_ptr<CommandModel> model);
    bool show(tc_ui_document_handle document, tc_ui_point position, tc_ui_rect viewport,
              bool dismiss_on_outside = true);
    bool dismiss(tc_ui_document_handle document,
                 tc_ui_overlay_dismiss_reason reason = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC);
    bool open() const { return open_; }
    size_t current_index() const { return current_; }
    float scroll_offset() const { return scroll_offset_; }
    float content_height() const { return content_height_; }
    void set_max_visible_height(float height);
    float max_visible_height() const { return max_visible_height_; }

    Signal<Menu&, size_t, CommandId, const CommandData&>& activated() { return activated_; }
    Signal<Menu&, tc_ui_overlay_dismiss_reason>& dismissed() { return dismissed_; }
    Signal<Menu&, int>& adjacent_requested() { return adjacent_requested_; }

    tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document_handle document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document_handle document, const tc_ui_key_event* event) override;
    tc_widget_handle hit_test(tc_ui_document_handle document, float x, float y) override;
    void overlay_dismissed(tc_ui_document_handle document, tc_ui_overlay_dismiss_reason reason) override;
    void on_destroy(tc_ui_document_handle document) override;

  private:
    friend class MenuBar;
    void set_anchor_owner(tc_widget_handle handle) { anchor_owner_ = handle; }
    Menu(std::shared_ptr<CommandModel> model, Menu* parent,
         std::unordered_set<const CommandModel*> ancestors);
    void connect_model();
    void disconnect_model();
    void sync_model();
    void rebuild_geometry(tc_ui_document_handle document);
    size_t index_at(float x, float y) const;
    size_t next_selectable(size_t from, int direction) const;
    void set_current(size_t index);
    void ensure_current_visible();
    bool activate_current(tc_ui_document_handle document);
    bool activate_index(tc_ui_document_handle document, size_t index);
    bool open_submenu(tc_ui_document_handle document, size_t index, bool select_first);
    void close_submenu(tc_ui_document_handle document);
    Menu* root_menu();

};

} // namespace termin::gui_native
