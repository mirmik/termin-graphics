#pragma once

#include <cstdint>
#include <string>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class IconButton : public NativeWidget {
private:
    std::string icon_;
    uint32_t texture_id_ = 0;
    bool active_ = false;
    bool pressed_ = false;
    Signal<IconButton&> clicked_;

public:
    explicit IconButton(std::string icon = {});
    void set_icon(std::string icon);
    void set_texture(uint32_t texture_id);
    void set_active(bool active);
    bool active() const { return active_; }
    Signal<IconButton&>& clicked() { return clicked_; }
    const Signal<IconButton&>& clicked() const { return clicked_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
};
} // namespace termin::gui_native
