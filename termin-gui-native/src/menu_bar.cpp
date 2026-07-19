#include "widgets_internal.hpp"

#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace termin::gui_native {
using namespace detail;

namespace {

std::string normalize_shortcut(std::string_view shortcut) {
    std::string result;
    result.reserve(shortcut.size());
    for (char value : shortcut) {
        if (!std::isspace(static_cast<unsigned char>(value)))
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(value))));
    }
    return result;
}

bool shortcut_matches(std::string_view descriptor, int32_t key, int32_t modifiers) {
    const std::string normalized = normalize_shortcut(descriptor);
    if (normalized.empty())
        return false;
    int32_t expected_modifiers = 0;
    size_t start = 0;
    size_t split = normalized.find('+');
    while (split != std::string::npos) {
        const std::string_view token(normalized.data() + start, split - start);
        if (token == "CTRL" || token == "CONTROL")
            expected_modifiers |= TC_UI_MOD_CTRL;
        else if (token == "SHIFT")
            expected_modifiers |= TC_UI_MOD_SHIFT;
        else if (token == "ALT")
            expected_modifiers |= TC_UI_MOD_ALT;
        else if (token == "SUPER" || token == "CMD" || token == "META")
            expected_modifiers |= TC_UI_MOD_SUPER;
        else
            return false;
        start = split + 1;
        split = normalized.find('+', start);
    }
    const std::string_view key_token(normalized.data() + start, normalized.size() - start);
    int32_t expected_key = TC_UI_KEY_UNKNOWN;
    if (key_token.size() == 1)
        expected_key = static_cast<unsigned char>(key_token.front());
    else if (key_token == "ENTER" || key_token == "RETURN")
        expected_key = TC_UI_KEY_ENTER;
    else if (key_token == "ESC" || key_token == "ESCAPE")
        expected_key = TC_UI_KEY_ESCAPE;
    else if (key_token == "DELETE" || key_token == "DEL")
        expected_key = TC_UI_KEY_DELETE;
    else if (key_token == "LEFT")
        expected_key = TC_UI_KEY_LEFT;
    else if (key_token == "RIGHT")
        expected_key = TC_UI_KEY_RIGHT;
    else if (key_token == "UP")
        expected_key = TC_UI_KEY_UP_ARROW;
    else if (key_token == "DOWN")
        expected_key = TC_UI_KEY_DOWN_ARROW;
    else if (key_token == "HOME")
        expected_key = TC_UI_KEY_HOME;
    else if (key_token == "END")
        expected_key = TC_UI_KEY_END;
    else if (key_token.size() >= 2 && key_token.front() == 'F') {
        int function_number = 0;
        for (const char digit : key_token.substr(1)) {
            if (!std::isdigit(static_cast<unsigned char>(digit)))
                return false;
            function_number = function_number * 10 + (digit - '0');
        }
        if (function_number < 1 || function_number > 12)
            return false;
        expected_key = TC_UI_KEY_F1 + function_number - 1;
    } else
        return false;
    constexpr int32_t known_modifiers =
        TC_UI_MOD_CTRL | TC_UI_MOD_SHIFT | TC_UI_MOD_ALT | TC_UI_MOD_SUPER;
    const int32_t normalized_key = key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key;
    return normalized_key == expected_key && (modifiers & known_modifiers) == expected_modifiers;
}

} // namespace

MenuBar::MenuBar() : NativeWidget("MenuBar") {
    set_style_role(TC_UI_STYLE_PANEL);
    set_cursor_intent(TC_UI_CURSOR_HAND);
    set_focusable(true);
    set_preferred_size(tc_ui_size{400.0f, item_height_});
}

void MenuBar::validate_entries(const std::vector<MenuBarEntry>& entries) {
    std::unordered_set<std::string> identifiers;
    for (const MenuBarEntry& entry : entries) {
        if (entry.stable_id.empty() || entry.label.empty() || !entry.menu) {
            tc_log_error("[termin-gui-native] MenuBar rejected incomplete entry");
            throw std::invalid_argument("menu bar entries require stable id, label, and model");
        }
        if (!identifiers.insert(entry.stable_id).second) {
            tc_log_error("[termin-gui-native] MenuBar rejected duplicate stable id");
            throw std::invalid_argument("menu bar stable ids must be unique");
        }
    }
}

void MenuBar::set_entries(std::vector<MenuBarEntry> entries) {
    if (menu_open()) {
        tc_log_error("[termin-gui-native] MenuBar entries cannot change while a menu is open");
        throw std::logic_error("menu bar entries cannot change while a menu is open");
    }
    validate_entries(entries);
    entries_ = std::move(entries);
    hovered_ = SIZE_MAX;
    item_rects_.clear();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void MenuBar::add_menu(MenuBarEntry entry) {
    std::vector<MenuBarEntry> next = entries_;
    next.push_back(std::move(entry));
    set_entries(std::move(next));
}

void MenuBar::clear() {
    entries_.clear();
    hovered_ = SIZE_MAX;
    item_rects_.clear();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size MenuBar::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    float width = 0.0f;
    for (const MenuBarEntry& entry : entries_) {
        tc_ui_text_metrics metrics{};
        measure_text(document, entry.label, style.font_size, metrics);
        width += metrics.width + padding_x_ * 2.0f;
    }
    return clamp_size(
        tc_ui_size{std::max(width, style.min_width), std::max(item_height_, style.min_height)},
        constraints);
}

void MenuBar::compute_item_rects(tc_ui_document* document) {
    const tc_ui_style style = computed_style(document);
    item_rects_.clear();
    item_rects_.reserve(entries_.size());
    float x = bounds().x;
    for (const MenuBarEntry& entry : entries_) {
        tc_ui_text_metrics metrics{};
        measure_text(document, entry.label, style.font_size, metrics);
        const float width = metrics.width + padding_x_ * 2.0f;
        item_rects_.push_back(tc_ui_rect{x, bounds().y, width, bounds().height});
        x += width;
    }
}

void MenuBar::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    compute_item_rects(document);
}

void MenuBar::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    if (item_rects_.size() != entries_.size())
        compute_item_rects(document);
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    for (size_t index = 0; index < entries_.size(); ++index) {
        const tc_ui_rect rect = item_rects_[index];
        if (index == hovered_ || index == open_index_) {
            tc_ui_color highlight = style.accent;
            highlight.a *= index == open_index_ ? 0.34f : 0.20f;
            tc_ui_painter_fill_rect(context, rect, highlight);
        }
        tc_ui_painter_draw_text(context, entries_[index].label.c_str(),
                                tc_ui_point{rect.x + padding_x_, rect.y + rect.height * 0.68f},
                                style.font_size, style.foreground);
    }
}

size_t MenuBar::index_at(float x, float y) const {
    for (size_t index = 0; index < item_rects_.size(); ++index) {
        if (rect_contains(item_rects_[index], x, y))
            return index;
    }
    return SIZE_MAX;
}

bool MenuBar::open_menu(tc_ui_document* document, size_t index, bool select_first) {
    if (index >= entries_.size())
        return false;
    if (!tc_widget_handle_is_invalid(popup_handle_))
        close_menu(document);
    auto popup = std::make_unique<Menu>(entries_[index].menu);
    const tc_widget_handle handle = tc_ui_document_adopt_widget(
        document, popup->c_widget(), &Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(handle))
        return false;
    popup_handle_ = handle;
    Menu* popup_ptr = popup.release();
    popup_ptr->set_anchor_owner(this->handle());
    popup_activated_connection_ = popup_ptr->activated().connect(
        [this, index](Menu&, size_t, CommandId id, const CommandData& command) {
            activated_.emit(*this, index, id, command);
        });
    popup_dismissed_connection_ =
        popup_ptr->dismissed().connect([this](Menu&, tc_ui_overlay_dismiss_reason) {
            open_index_ = SIZE_MAX;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        });
    popup_adjacent_connection_ = popup_ptr->adjacent_requested().connect(
        [this, document](Menu&, int direction) { switch_menu(document, direction); });
    const tc_widget* root = c_widget();
    while (root->parent)
        root = root->parent;
    const tc_ui_rect viewport = root->bounds;
    const tc_ui_rect anchor = item_rects_[index];
    open_index_ = index;
    if (!popup_ptr->show(document, tc_ui_point{anchor.x, anchor.y + anchor.height}, viewport,
                         true)) {
        tc_ui_document_destroy_widget(document, handle);
        popup_handle_ = tc_widget_handle_invalid();
        open_index_ = SIZE_MAX;
        return false;
    }
    if (select_first) {
        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.key = TC_UI_KEY_HOME;
        popup_ptr->key_event(document, &key);
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return true;
}

void MenuBar::close_menu(tc_ui_document* document) {
    if (tc_widget_handle_is_invalid(popup_handle_)) {
        open_index_ = SIZE_MAX;
        return;
    }
    const tc_widget_handle handle = popup_handle_;
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    if (widget) {
        auto* popup = dynamic_cast<Menu*>(native_widget_body(widget));
        if (popup && popup->open())
            popup->dismiss(document);
    }
    if (tc_ui_document_is_alive(document, handle))
        tc_ui_document_destroy_widget(document, handle);
    popup_handle_ = tc_widget_handle_invalid();
    open_index_ = SIZE_MAX;
    popup_activated_connection_ = 0;
    popup_dismissed_connection_ = 0;
    popup_adjacent_connection_ = 0;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void MenuBar::switch_menu(tc_ui_document* document, int direction) {
    if (entries_.empty())
        return;
    size_t index = open_index_;
    if (index == SIZE_MAX)
        index = direction > 0 ? 0 : entries_.size() - 1;
    else
        index = direction > 0 ? (index + 1) % entries_.size()
                              : (index + entries_.size() - 1) % entries_.size();
    open_menu(document, index, true);
}

tc_ui_event_result MenuBar::pointer_event(tc_ui_document* document,
                                          const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_MOVE) {
        const size_t next = index_at(event->x, event->y);
        if (next != hovered_) {
            hovered_ = next;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        if (open_index_ != SIZE_MAX && next != SIZE_MAX && next != open_index_)
            open_menu(document, next);
        return next == SIZE_MAX ? TC_UI_EVENT_IGNORED : TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN && event->button == pointer_button_value(PointerButton::Left)) {
        const size_t index = index_at(event->x, event->y);
        if (index == SIZE_MAX)
            return TC_UI_EVENT_IGNORED;
        tc_ui_document_set_focus(document, handle());
        if (index == open_index_)
            close_menu(document);
        else
            open_menu(document, index);
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result MenuBar::key_event(tc_ui_document* document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN)
        return TC_UI_EVENT_IGNORED;
    if (event->key == TC_UI_KEY_LEFT || event->key == TC_UI_KEY_RIGHT) {
        switch_menu(document, event->key == TC_UI_KEY_RIGHT ? 1 : -1);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_DOWN_ARROW || event->key == TC_UI_KEY_ENTER) {
        const size_t index = open_index_ != SIZE_MAX ? open_index_ : 0;
        return open_menu(document, index, true) ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    if (dispatch_shortcut(event->key, event->modifiers))
        return TC_UI_EVENT_HANDLED;
    return TC_UI_EVENT_IGNORED;
}

bool MenuBar::dispatch_shortcut_in(const std::shared_ptr<CommandModel>& model, int32_t key,
                                   int32_t modifiers,
                                   std::unordered_set<const CommandModel*>& visited,
                                   size_t menu_index) {
    if (!model || !visited.insert(model.get()).second)
        return false;
    for (size_t index = 0; index < model->size(); ++index) {
        const Command& command = model->command_at(index);
        if (command.data.kind != CommandKind::Action || !command.data.enabled)
            continue;
        if (command.data.submenu &&
            dispatch_shortcut_in(command.data.submenu, key, modifiers, visited, menu_index))
            return true;
        if (!shortcut_matches(command.data.shortcut, key, modifiers))
            continue;
        const CommandId id = command.id;
        if (command.data.checkable)
            model->set_checked(id, !command.data.checked);
        const CommandData snapshot = model->command(id).data;
        activated_.emit(*this, menu_index, id, snapshot);
        return true;
    }
    return false;
}

bool MenuBar::dispatch_shortcut(int32_t key, int32_t modifiers) {
    std::unordered_set<const CommandModel*> visited;
    for (size_t index = 0; index < entries_.size(); ++index) {
        if (dispatch_shortcut_in(entries_[index].menu, key, modifiers, visited, index))
            return true;
    }
    return false;
}

void MenuBar::on_destroy(tc_ui_document* document) { close_menu(document); }

} // namespace termin::gui_native
