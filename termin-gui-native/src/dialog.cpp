#include "widgets_internal.hpp"

#include <stdexcept>
#include <unordered_set>

namespace termin::gui_native {
using namespace detail;

Dialog::Dialog(std::string title) : NativeWidget("Dialog"), title_(std::move(title)) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_preferred_size(tc_ui_size{420.0f, 220.0f});
}

void Dialog::set_title(std::string title) {
    if (!valid_utf8(title)) {
        tc_log_error("[termin-gui-native] Dialog rejected invalid UTF-8 title");
        throw std::invalid_argument("dialog title must be valid UTF-8");
    }
    title_ = std::move(title);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void Dialog::validate_actions(const std::vector<DialogAction>& actions) {
    if (actions.empty()) {
        tc_log_error("[termin-gui-native] Dialog requires at least one action");
        throw std::invalid_argument("dialog requires at least one action");
    }
    std::unordered_set<std::string> identifiers;
    size_t defaults = 0;
    size_t cancels = 0;
    for (const DialogAction& action : actions) {
        if (action.stable_id.empty() || action.label.empty() || !valid_utf8(action.stable_id) ||
            !valid_utf8(action.label)) {
            tc_log_error("[termin-gui-native] Dialog rejected incomplete or invalid action");
            throw std::invalid_argument("dialog actions require valid stable id and label");
        }
        if (!identifiers.insert(action.stable_id).second) {
            tc_log_error("[termin-gui-native] Dialog rejected duplicate action stable id");
            throw std::invalid_argument("dialog action stable ids must be unique");
        }
        defaults += action.is_default ? 1 : 0;
        cancels += action.is_cancel ? 1 : 0;
    }
    if (defaults > 1 || cancels > 1) {
        tc_log_error("[termin-gui-native] Dialog rejected ambiguous default/cancel actions");
        throw std::invalid_argument("dialog supports at most one default and cancel action");
    }
}

void Dialog::set_actions(std::vector<DialogAction> actions) {
    if (open_) {
        tc_log_error("[termin-gui-native] Dialog actions cannot change while open");
        throw std::logic_error("dialog actions cannot change while open");
    }
    validate_actions(actions);
    if (document())
        destroy_buttons(document());
    actions_ = std::move(actions);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void Dialog::set_content(NativeWidget& content) {
    if (!document() || content.document() != document()) {
        tc_log_error("[termin-gui-native] Dialog content must be adopted by the same document");
        throw std::invalid_argument("dialog content must share the adopted document");
    }
    if (open_) {
        tc_log_error("[termin-gui-native] Dialog content cannot change while open");
        throw std::logic_error("dialog content cannot change while open");
    }
    if (tc_widget_handle_eq(content_handle_, content.handle()))
        return;
    if (!tc_widget_handle_is_invalid(content_handle_) &&
        tc_ui_document_is_alive(document(), content_handle_)) {
        tc_ui_document_destroy_widget_recursive(document(), content_handle_);
    }
    content_handle_ = content.handle();
    if (!tc_widget_append_child(c_widget(), content.c_widget())) {
        content_handle_ = tc_widget_handle_invalid();
        tc_log_error("[termin-gui-native] Dialog failed to attach content");
        throw std::runtime_error("failed to attach dialog content");
    }
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size Dialog::action_button_size(tc_ui_document* document, const DialogAction& action) {
    tc_ui_text_metrics metrics{};
    const bool has_metrics = measure_text(document, action.label, 14.0f, metrics);
    const float label_width = has_metrics ? metrics.width : static_cast<float>(action.label.size()) * 7.0f;
    return tc_ui_size{std::max(40.0f, label_width + 24.0f), 30.0f};
}

const DialogAction* Dialog::default_action() const {
    for (const DialogAction& action : actions_)
        if (action.is_default)
            return &action;
    return nullptr;
}

const DialogAction* Dialog::cancel_action() const {
    for (const DialogAction& action : actions_)
        if (action.is_cancel)
            return &action;
    return nullptr;
}

bool Dialog::ensure_buttons(tc_ui_document* document) {
    if (button_handles_.size() == actions_.size())
        return true;
    destroy_buttons(document);
    button_handles_.reserve(actions_.size());
    button_connections_.reserve(actions_.size());
    for (size_t index = 0; index < actions_.size(); ++index) {
        auto button = std::make_unique<Button>(actions_[index].label);
        if (actions_[index].is_default)
            button->set_accent(Color{0.22f, 0.48f, 0.86f, 1.0f});
        const tc_widget_handle handle = tc_ui_document_adopt_widget(
            document, button->c_widget(), &Widget::delete_owned_widget);
        if (tc_widget_handle_is_invalid(handle)) {
            tc_log_error("[termin-gui-native] Dialog failed to adopt action button");
            destroy_buttons(document);
            return false;
        }
        Button* body = button.release();
        if (!tc_widget_append_child(c_widget(), body->c_widget())) {
            tc_log_error("[termin-gui-native] Dialog failed to attach action button");
            tc_ui_document_destroy_widget(document, handle);
            destroy_buttons(document);
            return false;
        }
        const std::string stable_id = actions_[index].stable_id;
        button_handles_.push_back(handle);
        button_connections_.push_back(body->clicked().connect(
            [this, stable_id](Button&) { activate(stable_id, this->document()); }));
    }
    return true;
}

void Dialog::destroy_buttons(tc_ui_document* document) {
    for (size_t index = 0; index < button_handles_.size(); ++index) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, button_handles_[index]);
        if (!widget)
            continue;
        auto* button = dynamic_cast<Button*>(native_widget_body(widget));
        if (button && index < button_connections_.size())
            button->clicked().disconnect(button_connections_[index]);
        tc_ui_document_destroy_widget_recursive(document, button_handles_[index]);
    }
    button_handles_.clear();
    button_connections_.clear();
}

tc_ui_size Dialog::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    if (!ensure_buttons(document))
        return clamp_size(preferred_size(), constraints);
    float content_width = 0.0f;
    float content_height = 0.0f;
    if (tc_widget* content = tc_ui_document_resolve_widget(document, content_handle_)) {
        const tc_ui_size measured = measure_widget(content, document, unconstrained());
        content_width = measured.width;
        content_height = measured.height;
    }
    float button_width = 0.0f;
    for (const DialogAction& action : actions_)
        button_width += action_button_size(document, action).width;
    if (button_handles_.size() > 1)
        button_width += button_spacing_ * static_cast<float>(button_handles_.size() - 1);
    const tc_ui_size wanted{
        std::max(min_width_, std::max(content_width, button_width) + padding_ * 2),
        title_height_ + content_height + padding_ + button_bar_height_};
    return clamp_size(wanted, constraints);
}

void Dialog::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (!ensure_buttons(document))
        return;
    const float content_y = rect.y + title_height_;
    const float content_height = std::max(0.0f, rect.height - title_height_ - button_bar_height_);
    if (tc_widget* content = tc_ui_document_resolve_widget(document, content_handle_)) {
        layout_widget(content, document,
                      tc_ui_rect{rect.x + padding_, content_y, rect.width - padding_ * 2,
                                 std::max(0.0f, content_height - padding_)});
    }
    float total_width = 0.0f;
    std::vector<tc_ui_size> sizes;
    sizes.reserve(button_handles_.size());
    for (const DialogAction& action : actions_) {
        const tc_ui_size size = action_button_size(document, action);
        sizes.push_back(size);
        total_width += size.width;
    }
    if (sizes.size() > 1)
        total_width += button_spacing_ * static_cast<float>(sizes.size() - 1);
    float x = rect.x + rect.width - padding_ - total_width;
    for (size_t index = 0; index < button_handles_.size(); ++index) {
        tc_widget* button = tc_ui_document_resolve_widget(document, button_handles_[index]);
        if (!button)
            continue;
        const float y = rect.y + rect.height - button_bar_height_ +
                        (button_bar_height_ - sizes[index].height) * 0.5f;
        layout_widget(button, document, tc_ui_rect{x, y, sizes[index].width, sizes[index].height});
        x += sizes[index].width + button_spacing_;
    }
}

void Dialog::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rounded_rect(context, bounds(), style.corner_radius, style.background);
    if (style.border_width > 0.0f && color_visible(style.border)) {
        tc_ui_painter_stroke_rounded_rect(
            context, bounds(), style.corner_radius, style.border, style.border_width);
    }
    tc_ui_color title_background = style.accent;
    title_background.a *= 0.22f;
    tc_ui_painter_fill_rect(context,
                            tc_ui_rect{bounds().x, bounds().y, bounds().width, title_height_},
                            title_background);
    if (!title_.empty()) {
        tc_ui_painter_draw_text(
            context, title_.c_str(),
            tc_ui_point{bounds().x + padding_, bounds().y + title_height_ * 0.68f},
            style.font_size + 2.0f, style.foreground);
    }
    tc_ui_painter_push_clip(context, bounds());
    if (tc_widget* content = tc_ui_document_resolve_widget(document, content_handle_))
        paint_widget(content, document, context);
    for (tc_widget_handle handle : button_handles_) {
        if (tc_widget* button = tc_ui_document_resolve_widget(document, handle))
            paint_widget(button, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_widget_handle Dialog::hit_test(tc_ui_document* document, float x, float y) {
    if (!visible() || !rect_contains(bounds(), x, y))
        return tc_widget_handle_invalid();
    for (size_t index = child_count(); index > 0; --index) {
        tc_widget* child = child_at(index - 1);
        if (!child || !tc_widget_is_visible(child) || !child->vtable ||
            !child->vtable->hit_test)
            continue;
        const tc_widget_handle hit = child->vtable->hit_test(child, document, x, y);
        if (!tc_widget_handle_is_invalid(hit))
            return hit;
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

bool Dialog::show(tc_ui_document* document, tc_ui_rect viewport) {
    if (!document || !tc_ui_document_is_alive(document, handle()) || open_) {
        tc_log_error("[termin-gui-native] Dialog show requires a live closed adopted widget");
        return false;
    }
    viewport_ = viewport;
    previous_focus_ = tc_ui_document_focused_widget(document);
    has_result_ = false;
    has_pending_result_ = false;
    const tc_ui_size wanted = measure(document, unconstrained());
    const float width = std::min(wanted.width, viewport.width);
    const float height = std::min(wanted.height, viewport.height);
    layout(document, tc_ui_rect{viewport.x + (viewport.width - width) * 0.5f,
                                viewport.y + (viewport.height - height) * 0.5f, width, height});
    open_ = tc_ui_document_show_overlay(document, handle(), TC_UI_OVERLAY_MODAL);
    if (!open_)
        return false;
    const DialogAction* selected = default_action();
    if (selected) {
        const size_t index = static_cast<size_t>(selected - actions_.data());
        if (index < button_handles_.size())
            tc_ui_document_set_focus(document, button_handles_[index]);
    } else {
        tc_ui_document_focus_next(document);
    }
    return true;
}

bool Dialog::close(tc_ui_document* document) {
    if (!open_)
        return false;
    has_pending_result_ = true;
    pending_result_ = DialogResult{{}, DialogDismissReason::Programmatic};
    return tc_ui_document_dismiss_overlay(document, handle(), TC_UI_OVERLAY_DISMISS_PROGRAMMATIC);
}

bool Dialog::activate(std::string_view action_id, tc_ui_document* document) {
    if (!open_)
        return false;
    for (const DialogAction& action : actions_) {
        if (action.stable_id != action_id)
            continue;
        if (!before_action(action))
            return false;
        has_pending_result_ = true;
        pending_result_ = DialogResult{action.stable_id, DialogDismissReason::Action};
        return tc_ui_document_dismiss_overlay(document, handle(),
                                              TC_UI_OVERLAY_DISMISS_PROGRAMMATIC);
    }
    tc_log_error("[termin-gui-native] Dialog activation referenced unknown action");
    return false;
}

bool Dialog::before_action(const DialogAction&) { return true; }

tc_ui_event_result Dialog::key_event(tc_ui_document* document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN || event->key != TC_UI_KEY_ENTER)
        return TC_UI_EVENT_IGNORED;
    const DialogAction* action = default_action();
    return action && activate(action->stable_id, document) ? TC_UI_EVENT_HANDLED
                                                           : TC_UI_EVENT_IGNORED;
}

void Dialog::deliver_result(tc_ui_document* document, DialogResult result) {
    if (has_result_)
        return;
    has_result_ = true;
    result_ = std::move(result);
    if (tc_ui_document_is_alive(document, previous_focus_))
        tc_ui_document_set_focus(document, previous_focus_);
    previous_focus_ = tc_widget_handle_invalid();
    finished_.emit(*this, result_);
}

void Dialog::overlay_dismissed(tc_ui_document* document, tc_ui_overlay_dismiss_reason reason) {
    open_ = false;
    DialogResult result;
    if (has_pending_result_) {
        result = std::move(pending_result_);
        has_pending_result_ = false;
    } else if (reason == TC_UI_OVERLAY_DISMISS_ESCAPE) {
        const DialogAction* cancel = cancel_action();
        result =
            DialogResult{cancel ? cancel->stable_id : std::string{}, DialogDismissReason::Escape};
    } else {
        result = DialogResult{{}, DialogDismissReason::Programmatic};
    }
    deliver_result(document, std::move(result));
}

void Dialog::on_destroy(tc_ui_document* document) {
    open_ = false;
    destroy_buttons(document);
    if (!tc_widget_handle_is_invalid(content_handle_) &&
        tc_ui_document_is_alive(document, content_handle_)) {
        tc_ui_document_destroy_widget_recursive(document, content_handle_);
    }
    content_handle_ = tc_widget_handle_invalid();
}

} // namespace termin::gui_native
