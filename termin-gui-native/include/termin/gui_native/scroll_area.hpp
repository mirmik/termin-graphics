#pragma once

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {

    enum class ScrollBarPolicy {
        Auto,
        Always,
        Hidden,
    };

    class ScrollArea : public NativeWidget {
    private:
        enum class DragAxis {
            None,
            Horizontal,
            Vertical,
        };

        tc_ui_size content_size_{0.0f, 0.0f};
        float scroll_x_ = 0.0f;
        float scroll_y_ = 0.0f;
        float wheel_step_ = 48.0f;
        bool horizontal_scroll_enabled_ = true;
        bool vertical_scroll_enabled_ = true;
        ScrollBarPolicy horizontal_scrollbar_policy_ = ScrollBarPolicy::Auto;
        ScrollBarPolicy vertical_scrollbar_policy_ = ScrollBarPolicy::Auto;
        DragAxis drag_axis_ = DragAxis::None;
        float drag_pointer_origin_ = 0.0f;
        float drag_scroll_origin_ = 0.0f;
        Signal<ScrollArea&, float, float> changed_;

    public:
        explicit ScrollArea(const char* debug_name = nullptr);
        void set_content(tc_widget_handle handle);
        void set_content(const Widget& widget) {
            set_content(widget.handle());
        }
        tc_widget_handle content() const {
            return child_handle_at(0);
        }
        void set_scroll_axes(bool horizontal, bool vertical);
        bool horizontal_scroll_enabled() const {
            return horizontal_scroll_enabled_;
        }
        bool vertical_scroll_enabled() const {
            return vertical_scroll_enabled_;
        }
        void set_scrollbar_policy(ScrollBarPolicy horizontal, ScrollBarPolicy vertical);
        ScrollBarPolicy horizontal_scrollbar_policy() const {
            return horizontal_scrollbar_policy_;
        }
        ScrollBarPolicy vertical_scrollbar_policy() const {
            return vertical_scrollbar_policy_;
        }
        bool horizontal_scrollbar_visible() const;
        bool vertical_scrollbar_visible() const;
        void set_scroll(float x, float y);
        float scroll_x() const {
            return scroll_x_;
        }
        float scroll_y() const {
            return scroll_y_;
        }
        tc_ui_size content_size() const {
            return content_size_;
        }
        Signal<ScrollArea&, float, float>& changed() {
            return changed_;
        }
        const Signal<ScrollArea&, float, float>& changed() const {
            return changed_;
        }
        bool ensure_visible(tc_widget_handle descendant);
        bool ensure_visible(const Widget& descendant) {
            return ensure_visible(descendant.handle());
        }
        tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
        void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
        void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
        tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;
        tc_widget_handle hit_test(tc_ui_document_handle document, float x, float y) override;
        tc_ui_event_result key_event(tc_ui_document_handle document, const tc_ui_key_event* event) override;
        void descendant_focused(tc_ui_document_handle document, tc_widget_handle descendant) override;

    private:
        float max_scroll_x() const;
        float max_scroll_y() const;
        bool apply_scroll(tc_ui_document_handle document, float x, float y);
        void layout_content(tc_ui_document_handle document);
        tc_ui_rect horizontal_track_rect() const;
        tc_ui_rect vertical_track_rect() const;
        tc_ui_rect horizontal_thumb_rect() const;
        tc_ui_rect vertical_thumb_rect() const;
    };
} // namespace termin::gui_native
