#pragma once

#include <string>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class Button : public NativeWidget {
  private:
    std::string text_;
    bool pressed_ = false;
    Signal<Button&> clicked_;

  public:
    explicit Button(std::string text = {});
    Button(std::string text, Color fill);
    explicit Button(Color fill);
    Button& set_accent(Color color);
    Button& set_text(std::string text);
    Signal<Button&>& clicked() { return clicked_; }
    const Signal<Button&>& clicked() const { return clicked_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;

};
} // namespace termin::gui_native
