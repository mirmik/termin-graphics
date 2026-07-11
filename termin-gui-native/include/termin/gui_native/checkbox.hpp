#pragma once

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class Checkbox : public NativeWidget {
private:
    bool checked_ = false;
    bool pressed_ = false;
    Signal<Checkbox&, bool> changed_;

public:
    explicit Checkbox(bool checked = false);
    bool checked() const { return checked_; }
    void set_checked(bool checked);
    Signal<Checkbox&, bool>& changed() { return changed_; }
    const Signal<Checkbox&, bool>& changed() const { return changed_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
};
} // namespace termin::gui_native
