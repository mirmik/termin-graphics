#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
    class IconButton : public NativeWidget {
    private:
        std::string icon_;
        uint32_t texture_id_ = 0;
        bool active_ = false;
        bool pressed_ = false;
        bool keyboard_pressed_ = false;
        std::string tooltip_;
        std::optional<SrgbColor> background_color_;
        std::optional<SrgbColor> hover_color_;
        std::optional<SrgbColor> pressed_color_;
        std::optional<SrgbColor> active_color_;
        std::optional<SrgbColor> icon_color_;
        std::optional<float> corner_radius_;
        std::optional<float> font_size_;
        Signal<IconButton&> clicked_;

    public:
        explicit IconButton(std::string icon = {});
        void set_icon(std::string icon);
        void set_texture(uint32_t texture_id);
        void set_active(bool active);
        void set_tooltip(std::string tooltip);
        const std::string& tooltip() const {
            return tooltip_;
        }
        void set_background_color(SrgbColor color);
        void set_hover_color(SrgbColor color);
        void set_pressed_color(SrgbColor color);
        void set_active_color(SrgbColor color);
        void set_icon_color(SrgbColor color);
        void set_corner_radius(float radius);
        void set_font_size(float size);
        bool active() const {
            return active_;
        }
        Signal<IconButton&>& clicked() {
            return clicked_;
        }
        const Signal<IconButton&>& clicked() const {
            return clicked_;
        }
        void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
        tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;
        tc_ui_event_result key_event(tc_ui_document_handle document, const tc_ui_key_event* event) override;
        void focus_event(tc_ui_document_handle document, bool focused) override;
    };
} // namespace termin::gui_native
