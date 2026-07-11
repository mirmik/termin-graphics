#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {

enum class DialogDismissReason { Action, Escape, Programmatic };

struct DialogAction {
    std::string stable_id;
    std::string label;
    bool is_default = false;
    bool is_cancel = false;
};

struct DialogResult {
    std::string action_id;
    DialogDismissReason reason = DialogDismissReason::Programmatic;
};

class Dialog : public NativeWidget {
  private:
    std::string title_;
    std::vector<DialogAction> actions_{{"ok", "OK", true, false}};
    tc_widget_handle content_handle_ = tc_widget_handle_invalid();
    std::vector<tc_widget_handle> button_handles_;
    std::vector<size_t> button_connections_;
    tc_widget_handle previous_focus_ = tc_widget_handle_invalid();
    tc_ui_rect viewport_{};
    DialogResult pending_result_{};
    DialogResult result_{};
    float title_height_ = 38.0f;
    float button_bar_height_ = 52.0f;
    float padding_ = 16.0f;
    float button_spacing_ = 8.0f;
    float min_width_ = 300.0f;
    bool has_pending_result_ = false;
    bool has_result_ = false;
    bool open_ = false;
    Signal<Dialog&, const DialogResult&> finished_;

  public:
    explicit Dialog(std::string title = {});
    ~Dialog() override = default;

    const std::string& title() const { return title_; }
    void set_title(std::string title);
    const std::vector<DialogAction>& actions() const { return actions_; }
    void set_actions(std::vector<DialogAction> actions);
    void set_content(NativeWidget& content);
    tc_widget_handle content_handle() const { return content_handle_; }
    bool show(tc_ui_document* document, tc_ui_rect viewport);
    bool close(tc_ui_document* document);
    bool activate(std::string_view action_id, tc_ui_document* document);
    bool open() const { return open_; }
    const DialogResult* result() const { return has_result_ ? &result_ : nullptr; }
    const std::vector<tc_widget_handle>& button_handles() const { return button_handles_; }

    Signal<Dialog&, const DialogResult&>& finished() { return finished_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    void overlay_dismissed(tc_ui_document* document, tc_ui_overlay_dismiss_reason reason) override;
    void on_destroy(tc_ui_document* document) override;

  protected:
    virtual bool before_action(const DialogAction& action);

  private:
    static void validate_actions(const std::vector<DialogAction>& actions);
    static tc_ui_size action_button_size(tc_ui_document* document, const DialogAction& action);
    bool ensure_buttons(tc_ui_document* document);
    void destroy_buttons(tc_ui_document* document);
    const DialogAction* default_action() const;
    const DialogAction* cancel_action() const;
    void deliver_result(tc_ui_document* document, DialogResult result);

};

} // namespace termin::gui_native
