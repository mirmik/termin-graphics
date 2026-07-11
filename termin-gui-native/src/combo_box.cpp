#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
class ComboBoxPopup final : public NativeWidget {
private:
    ComboBox& owner_;
    int hovered_ = -1;
    float scroll_y_ = 0.0f;

public:
    explicit ComboBoxPopup(ComboBox& owner)
        : NativeWidget("ComboBoxPopup"), owner_(owner) {}

    void paint(tc_ui_document* document, tc_ui_paint_context* context) override {
        const tc_ui_style style = owner_.computed_style(document);
        tc_ui_painter_fill_rect(context, bounds(), tc_ui_color {0.18f, 0.18f, 0.22f, 0.98f});
        tc_ui_painter_push_clip(context, bounds());
        for (size_t index = 0; index < owner_.items_.size(); ++index) {
            const float y = bounds().y + static_cast<float>(index) * owner_.item_height_ - scroll_y_;
            if (y + owner_.item_height_ < bounds().y || y > bounds().y + bounds().height) continue;
            if (static_cast<int>(index) == hovered_ || static_cast<int>(index) == owner_.selected_index_) {
                tc_ui_color color = style.accent;
                color.a = static_cast<int>(index) == hovered_ ? 0.45f : 0.25f;
                tc_ui_painter_fill_rect(context, tc_ui_rect {bounds().x, y, bounds().width, owner_.item_height_}, color);
            }
            tc_ui_painter_draw_text(
                context, owner_.items_[index].c_str(),
                tc_ui_point {bounds().x + 8.0f, y + owner_.item_height_ * 0.72f},
                style.font_size, style.foreground
            );
        }
        tc_ui_painter_pop_clip(context);
        tc_ui_painter_stroke_rect(context, bounds(), style.border, 1.0f);
    }

    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override {
        if (!event) return TC_UI_EVENT_IGNORED;
        if (event->type == TC_UI_POINTER_WHEEL) {
            const float content_height = static_cast<float>(owner_.items_.size()) * owner_.item_height_;
            scroll_y_ = clamp_float(
                scroll_y_ - event->wheel_y * owner_.item_height_ * 2.0f,
                0.0f,
                std::max(0.0f, content_height - bounds().height)
            );
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_MOVE) {
            const int next = static_cast<int>((event->y - bounds().y + scroll_y_) / owner_.item_height_);
            hovered_ = next >= 0 && next < static_cast<int>(owner_.items_.size()) ? next : -1;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
            const int index = static_cast<int>((event->y - bounds().y + scroll_y_) / owner_.item_height_);
            if (index >= 0 && index < static_cast<int>(owner_.items_.size())) {
                owner_.set_selected_index(index);
                owner_.hide_popup(document);
            }
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }

    void overlay_dismissed(tc_ui_document*, tc_ui_overlay_dismiss_reason) override {
        owner_.popup_dismissed();
    }

private:
};
ComboBox::ComboBox()
    : NativeWidget("ComboBox") {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_focusable(true);
    set_preferred_size(tc_ui_size {200.0f, 34.0f});
}

const std::string& ComboBox::item_text(size_t index) const {
    if (index >= items_.size()) throw std::out_of_range("ComboBox item index out of range");
    return items_[index];
}

std::string ComboBox::selected_text() const {
    return selected_index_ >= 0 && selected_index_ < static_cast<int>(items_.size())
        ? items_[selected_index_]
        : std::string {};
}

void ComboBox::add_item(std::string item) {
    if (!valid_utf8(item)) {
        tc_log_error("[termin-gui-native] ComboBox rejected invalid UTF-8 item");
        return;
    }
    items_.push_back(std::move(item));
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ComboBox::clear_items() {
    items_.clear();
    selected_index_ = -1;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void ComboBox::set_selected_index(int index) {
    if (index < -1 || index >= static_cast<int>(items_.size())) {
        tc_log_error("[termin-gui-native] ComboBox rejected invalid selected index");
        return;
    }
    if (selected_index_ == index) return;
    selected_index_ = index;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    const std::string text = selected_text();
    changed_.emit(*this, selected_index_, text);
}

tc_ui_size ComboBox::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    float max_width = 0.0f;
    for (const std::string& item : items_) {
        tc_ui_text_metrics metrics {};
        if (measure_text(document, item, style.font_size, metrics)) max_width = std::max(max_width, metrics.width);
    }
    tc_ui_size result {std::max(200.0f, max_width + 36.0f), std::max(34.0f, style.min_height)};
    return clamp_size(result, constraints);
}

void ComboBox::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const std::string text = selected_text().empty() ? "Select..." : selected_text();
    tc_ui_painter_fill_rounded_rect(context, bounds(), style.corner_radius, style.background);
    if (style.border_width > 0.0f) {
        tc_ui_painter_stroke_rounded_rect(
            context, bounds(), style.corner_radius, open_ ? style.accent : style.border,
            style.border_width);
    }
    tc_ui_painter_draw_text(context, text.c_str(), tc_ui_point {bounds().x + 8.0f, bounds().y + bounds().height * 0.68f}, style.font_size, style.foreground);
    tc_ui_painter_draw_text(context, open_ ? "^" : "v", tc_ui_point {bounds().x + bounds().width - 18.0f, bounds().y + bounds().height * 0.68f}, style.font_size, style.foreground);
}

bool ComboBox::show_popup(tc_ui_document* document) {
    if (items_.empty()) return false;
    if (tc_widget_handle_is_invalid(popup_handle_)) {
        auto popup = std::make_unique<ComboBoxPopup>(*this);
        popup_handle_ = tc_ui_document_adopt_widget(document, popup->c_widget());
        if (tc_widget_handle_is_invalid(popup_handle_)) return false;
        popup.release();
    }
    tc_widget* popup = tc_ui_document_resolve_widget(document, popup_handle_);
    if (!popup) return false;
    const float height = std::min(items_.size(), max_visible_items_) * item_height_;
    float y = bounds().y + bounds().height;
    const tc_widget* root = c_widget();
    while (root->parent) root = root->parent;
    if (y + height > root->bounds.y + root->bounds.height) y = bounds().y - height;
    tc_widget_set_bounds(popup, tc_ui_rect {bounds().x, y, bounds().width, height});
    open_ = tc_ui_document_show_overlay(document, popup_handle_, TC_UI_OVERLAY_DISMISS_ON_OUTSIDE);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return open_;
}

void ComboBox::hide_popup(tc_ui_document* document) {
    if (open_) tc_ui_document_dismiss_overlay(document, popup_handle_, TC_UI_OVERLAY_DISMISS_PROGRAMMATIC);
}

void ComboBox::popup_dismissed() {
    open_ = false;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_event_result ComboBox::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || event->type != TC_UI_POINTER_DOWN || !rect_contains(bounds(), event->x, event->y)) return TC_UI_EVENT_IGNORED;
    tc_ui_document_set_focus(document, handle());
    if (open_) hide_popup(document); else show_popup(document);
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result ComboBox::key_event(tc_ui_document* document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN) return TC_UI_EVENT_IGNORED;
    if (event->key == TC_UI_KEY_ESCAPE && open_) {
        hide_popup(document);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_ENTER) {
        if (open_) hide_popup(document); else show_popup(document);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_UP_ARROW && !items_.empty()) {
        set_selected_index(std::max(0, selected_index_ - 1));
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_DOWN_ARROW && !items_.empty()) {
        set_selected_index(std::min(static_cast<int>(items_.size()) - 1, selected_index_ + 1));
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

void ComboBox::on_destroy(tc_ui_document* document) {
    if (!tc_widget_handle_is_invalid(popup_handle_) && tc_ui_document_is_alive(document, popup_handle_)) {
        if (open_) tc_ui_document_dismiss_overlay(document, popup_handle_, TC_UI_OVERLAY_DISMISS_PROGRAMMATIC);
        tc_ui_document_destroy_widget(document, popup_handle_);
    }
    popup_handle_ = tc_widget_handle_invalid();
}
} // namespace termin::gui_native
