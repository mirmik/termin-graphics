#include "widgets_internal.hpp"

#include <cmath>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

Menu::Menu(std::shared_ptr<CommandModel> model)
    : Menu(model ? std::move(model) : std::make_shared<CommandModel>(), nullptr, {}) {}

Menu::Menu(std::shared_ptr<CommandModel> model, Menu* parent,
           std::unordered_set<const CommandModel*> ancestors)
    : NativeWidget("Menu"), model_(std::move(model)), parent_menu_(parent),
      ancestors_(std::move(ancestors)) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_cursor_intent(TC_UI_CURSOR_HAND);
    set_focusable(true);
    ancestors_.insert(model_.get());
    observed_revision_ = model_->revision();
    connect_model();
}

Menu::~Menu() { disconnect_model(); }

void Menu::connect_model() {
    if (model_connection_ != 0)
        return;
    model_connection_ = model_->changed().connect([this](CommandModel&, const CommandChange&) {
        observed_revision_ = model_->revision();
        current_ = SIZE_MAX;
        item_tops_.clear();
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    });
}

void Menu::disconnect_model() {
    if (model_ && model_connection_ != 0)
        model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void Menu::set_model(std::shared_ptr<CommandModel> model) {
    if (open_) {
        tc_log_error("[termin-gui-native] Menu model cannot change while overlay is open");
        throw std::logic_error("menu model cannot change while open");
    }
    disconnect_model();
    model_ = model ? std::move(model) : std::make_shared<CommandModel>();
    ancestors_.clear();
    ancestors_.insert(model_.get());
    observed_revision_ = model_->revision();
    current_ = SIZE_MAX;
    item_tops_.clear();
    connect_model();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void Menu::sync_model() {
    if (observed_revision_ == model_->revision())
        return;
    observed_revision_ = model_->revision();
    current_ = SIZE_MAX;
    item_tops_.clear();
}

void Menu::set_max_visible_height(float height) {
    if (!std::isfinite(height) || height <= 0.0f) {
        tc_log_error("[termin-gui-native] Menu rejected invalid maximum visible height");
        throw std::invalid_argument("menu maximum visible height must be finite and positive");
    }
    max_visible_height_ = height;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void Menu::rebuild_geometry(tc_ui_document* document) {
    sync_model();
    item_tops_.clear();
    item_tops_.reserve(model_->size() + 1);
    float y = padding_y_;
    for (const Command& command : model_->commands()) {
        item_tops_.push_back(y);
        y += command.data.kind == CommandKind::Separator ? separator_height_ : item_height_;
    }
    item_tops_.push_back(y);
    content_height_ = y + padding_y_;
    (void)document;
}

tc_ui_size Menu::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    rebuild_geometry(document);
    const tc_ui_style style = computed_style(document);
    float label_width = 0.0f;
    float shortcut_width = 0.0f;
    bool has_icon = false;
    bool has_check = false;
    bool has_submenu = false;
    for (const Command& command : model_->commands()) {
        if (command.data.kind == CommandKind::Separator)
            continue;
        tc_ui_text_metrics metrics{};
        measure_text(document, command.data.label, style.font_size, metrics);
        label_width = std::max(label_width, metrics.width);
        measure_text(document, command.data.shortcut, style.font_size * 0.9f, metrics);
        shortcut_width = std::max(shortcut_width, metrics.width);
        has_icon = has_icon || command.data.texture_id != 0 || !command.data.icon.empty();
        has_check = has_check || command.data.checkable;
        has_submenu = has_submenu || static_cast<bool>(command.data.submenu);
    }
    float width = padding_x_ * 2.0f + label_width;
    if (has_icon)
        width += item_height_ * 0.60f + 5.0f;
    if (has_check)
        width += item_height_ * 0.55f;
    if (shortcut_width > 0.0f)
        width += column_gap_ + shortcut_width;
    if (has_submenu)
        width += column_gap_;
    const float viewport_limit = viewport_.height > 0.0f
                                     ? std::max(item_height_, viewport_.height - 8.0f)
                                     : max_visible_height_;
    const float height = std::min(content_height_, std::min(max_visible_height_, viewport_limit));
    return clamp_size(tc_ui_size{std::max(min_width_, width), height}, constraints);
}

void Menu::layout(tc_ui_document* document, tc_ui_rect rect) {
    rebuild_geometry(document);
    NativeWidget::layout(document, rect);
    scroll_offset_ =
        clamp_float(scroll_offset_, 0.0f, std::max(0.0f, content_height_ - rect.height));
}

bool Menu::show(tc_ui_document* document, tc_ui_point position, tc_ui_rect viewport,
                bool dismiss_on_outside) {
    if (!document || !tc_ui_document_is_alive(document, handle())) {
        tc_log_error("[termin-gui-native] Menu must be adopted before show");
        return false;
    }
    viewport_ = viewport;
    const tc_ui_size wanted = measure(document, unconstrained());
    float x = position.x;
    float y = position.y;
    if (x + wanted.width > viewport.x + viewport.width)
        x = viewport.x + viewport.width - wanted.width;
    if (y + wanted.height > viewport.y + viewport.height)
        y = viewport.y + viewport.height - wanted.height;
    x = std::max(viewport.x, x);
    y = std::max(viewport.y, y);
    layout(document, tc_ui_rect{x, y, wanted.width, wanted.height});
    uint32_t flags = dismiss_on_outside ? TC_UI_OVERLAY_DISMISS_ON_OUTSIDE : 0;
    if (!tc_widget_handle_is_invalid(anchor_owner_))
        flags |= TC_UI_OVERLAY_ALLOW_ROOT_HIT;
    open_ = tc_ui_document_show_overlay(document, handle(), flags);
    if (open_)
        tc_ui_document_set_focus(document, handle());
    return open_;
}

bool Menu::dismiss(tc_ui_document* document, tc_ui_overlay_dismiss_reason reason) {
    if (!open_)
        return false;
    return tc_ui_document_dismiss_overlay(document, handle(), reason);
}

size_t Menu::index_at(float x, float y) const {
    if (!rect_contains(bounds(), x, y) || item_tops_.size() != model_->size() + 1)
        return SIZE_MAX;
    const float local_y = y - bounds().y + scroll_offset_;
    for (size_t i = 0; i < model_->size(); ++i) {
        if (local_y >= item_tops_[i] && local_y < item_tops_[i + 1])
            return model_->command_at(i).data.kind == CommandKind::Action ? i : SIZE_MAX;
    }
    return SIZE_MAX;
}

size_t Menu::next_selectable(size_t from, int direction) const {
    if (model_->empty())
        return SIZE_MAX;
    size_t index = from;
    for (size_t count = 0; count < model_->size(); ++count) {
        if (index == SIZE_MAX)
            index = direction > 0 ? 0 : model_->size() - 1;
        else
            index = direction > 0 ? (index + 1) % model_->size()
                                  : (index + model_->size() - 1) % model_->size();
        const CommandData& data = model_->command_at(index).data;
        if (data.kind == CommandKind::Action && data.enabled)
            return index;
    }
    return SIZE_MAX;
}

void Menu::set_current(size_t index) {
    if (index != SIZE_MAX && index >= model_->size())
        index = SIZE_MAX;
    if (current_ == index)
        return;
    current_ = index;
    ensure_current_visible();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void Menu::ensure_current_visible() {
    if (current_ == SIZE_MAX || item_tops_.size() != model_->size() + 1)
        return;
    const float top = item_tops_[current_];
    const float bottom = item_tops_[current_ + 1];
    if (top < scroll_offset_)
        scroll_offset_ = top;
    else if (bottom > scroll_offset_ + bounds().height)
        scroll_offset_ = bottom - bounds().height;
    scroll_offset_ =
        clamp_float(scroll_offset_, 0.0f, std::max(0.0f, content_height_ - bounds().height));
}

Menu* Menu::root_menu() {
    Menu* result = this;
    while (result->parent_menu_)
        result = result->parent_menu_;
    return result;
}

void Menu::close_submenu(tc_ui_document* document) {
    if (tc_widget_handle_is_invalid(child_handle_))
        return;
    tc_widget* child_widget = tc_ui_document_resolve_widget(document, child_handle_);
    if (child_widget) {
        auto* child = dynamic_cast<Menu*>(native_widget_body(child_widget));
        if (child && child->open_)
            child->dismiss(document);
        if (tc_ui_document_is_alive(document, child_handle_))
            tc_ui_document_destroy_widget(document, child_handle_);
    }
    child_handle_ = tc_widget_handle_invalid();
    child_index_ = SIZE_MAX;
}

bool Menu::open_submenu(tc_ui_document* document, size_t index, bool select_first) {
    if (index >= model_->size())
        return false;
    const CommandData& data = model_->command_at(index).data;
    if (!data.enabled || !data.submenu)
        return false;
    if (child_index_ == index && !tc_widget_handle_is_invalid(child_handle_)) {
        tc_widget* existing_widget = tc_ui_document_resolve_widget(document, child_handle_);
        auto* existing =
            existing_widget ? dynamic_cast<Menu*>(native_widget_body(existing_widget)) : nullptr;
        if (existing && existing->open_) {
            if (select_first) {
                existing->set_current(existing->next_selectable(SIZE_MAX, 1));
                tc_ui_document_set_focus(document, existing->handle());
            }
            return true;
        }
    }
    if (ancestors_.contains(data.submenu.get())) {
        tc_log_error("[termin-gui-native] Menu refused cyclic submenu model graph");
        return false;
    }
    close_submenu(document);
    auto child = std::unique_ptr<Menu>(new Menu(data.submenu, this, ancestors_));
    const tc_widget_handle handle = tc_ui_document_adopt_widget(
        document, child->c_widget(), &Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(handle))
        return false;
    child_handle_ = handle;
    child_index_ = index;
    Menu* child_ptr = child.release();
    const float item_y = bounds().y + item_tops_[index] - scroll_offset_;
    tc_ui_size child_size = child_ptr->measure(document, unconstrained());
    float x = bounds().x + bounds().width;
    if (x + child_size.width > viewport_.x + viewport_.width)
        x = bounds().x - child_size.width;
    if (!child_ptr->show(document, tc_ui_point{x, item_y}, viewport_, false)) {
        tc_ui_document_destroy_widget(document, handle);
        child_handle_ = tc_widget_handle_invalid();
        child_index_ = SIZE_MAX;
        return false;
    }
    if (select_first)
        child_ptr->set_current(child_ptr->next_selectable(SIZE_MAX, 1));
    return true;
}

bool Menu::activate_index(tc_ui_document* document, size_t index) {
    if (index >= model_->size())
        return false;
    const Command& command = model_->command_at(index);
    if (command.data.kind != CommandKind::Action || !command.data.enabled)
        return false;
    if (command.data.submenu)
        return open_submenu(document, index, true);
    const CommandId id = command.id;
    if (command.data.checkable)
        model_->set_checked(id, !command.data.checked);
    const Command& updated = model_->command(id);
    CommandData snapshot = updated.data;
    Menu* root = root_menu();
    const tc_widget_handle root_handle = root->handle();
    root->activated_.emit(*root, index, id, snapshot);
    tc_widget* root_widget = tc_ui_document_resolve_widget(document, root_handle);
    auto* live_root = root_widget ? dynamic_cast<Menu*>(native_widget_body(root_widget)) : nullptr;
    if (live_root && live_root->open_)
        live_root->dismiss(document, TC_UI_OVERLAY_DISMISS_PROGRAMMATIC);
    return true;
}

bool Menu::activate_current(tc_ui_document* document) { return activate_index(document, current_); }

void Menu::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    if (item_tops_.size() != model_->size() + 1)
        rebuild_geometry(document);
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
    tc_ui_painter_push_clip(context, bounds());
    bool has_icon = false;
    bool has_check = false;
    bool has_submenu = false;
    for (const Command& command : model_->commands()) {
        has_icon = has_icon || command.data.texture_id != 0 || !command.data.icon.empty();
        has_check = has_check || command.data.checkable;
        has_submenu = has_submenu || static_cast<bool>(command.data.submenu);
    }
    float label_x = bounds().x + padding_x_;
    if (has_check)
        label_x += item_height_ * 0.55f;
    if (has_icon)
        label_x += item_height_ * 0.60f + 5.0f;
    for (size_t index = 0; index < model_->size(); ++index) {
        const CommandData& data = model_->command_at(index).data;
        const float y = bounds().y + item_tops_[index] - scroll_offset_;
        const float height = item_tops_[index + 1] - item_tops_[index];
        if (y + height < bounds().y || y > bounds().y + bounds().height)
            continue;
        if (data.kind == CommandKind::Separator) {
            tc_ui_painter_draw_line(
                context, tc_ui_point{bounds().x + padding_x_, y + height / 2},
                tc_ui_point{bounds().x + bounds().width - padding_x_, y + height / 2}, style.border,
                1.0f);
            continue;
        }
        if (index == current_ && data.enabled) {
            tc_ui_color highlight = style.accent;
            highlight.a *= 0.24f;
            tc_ui_painter_fill_rect(context,
                                    tc_ui_rect{bounds().x + 2.0f, y, bounds().width - 4.0f, height},
                                    highlight);
        }
        tc_ui_color foreground = style.foreground;
        if (!data.enabled)
            foreground.a *= 0.45f;
        const float baseline = y + height * 0.68f;
        float prefix_x = bounds().x + padding_x_;
        if (has_check) {
            if (data.checkable && data.checked)
                tc_ui_painter_draw_text(context, "✓", tc_ui_point{prefix_x, baseline},
                                        style.font_size, foreground);
            prefix_x += item_height_ * 0.55f;
        }
        if (has_icon) {
            if (data.texture_id != 0) {
                const float extent = item_height_ * 0.55f;
                tc_ui_painter_draw_texture(
                    context, data.texture_id,
                    tc_ui_rect{prefix_x, y + (height - extent) / 2, extent, extent}, foreground,
                    TC_UI_TEXTURE_SAMPLING_LINEAR, false);
            } else if (!data.icon.empty()) {
                tc_ui_painter_draw_text(context, data.icon.c_str(), tc_ui_point{prefix_x, baseline},
                                        style.font_size, foreground);
            }
        }
        tc_ui_painter_draw_text(context, data.label.c_str(), tc_ui_point{label_x, baseline},
                                style.font_size, foreground);
        float right = bounds().x + bounds().width - padding_x_;
        if (has_submenu) {
            if (data.submenu)
                tc_ui_painter_draw_text(context, ">", tc_ui_point{right - 7.0f, baseline},
                                        style.font_size, foreground);
            right -= column_gap_;
        }
        if (!data.shortcut.empty()) {
            tc_ui_text_metrics metrics{};
            measure_text(document, data.shortcut, style.font_size * 0.9f, metrics);
            tc_ui_painter_draw_text(context, data.shortcut.c_str(),
                                    tc_ui_point{right - metrics.width, baseline},
                                    style.font_size * 0.9f, foreground);
        }
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result Menu::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_WHEEL && rect_contains(bounds(), event->x, event->y)) {
        scroll_offset_ = clamp_float(scroll_offset_ - event->wheel_y * item_height_ * 2.0f, 0.0f,
                                     std::max(0.0f, content_height_ - bounds().height));
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE) {
        const size_t index = index_at(event->x, event->y);
        set_current(index);
        if (index != SIZE_MAX && model_->command_at(index).data.submenu)
            open_submenu(document, index, false);
        else
            close_submenu(document);
        return rect_contains(bounds(), event->x, event->y) ? TC_UI_EVENT_HANDLED
                                                           : TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y))
        return TC_UI_EVENT_HANDLED;
    if (event->type == TC_UI_POINTER_UP && event->button == pointer_button_value(PointerButton::Left)) {
        const size_t index = index_at(event->x, event->y);
        return activate_index(document, index) ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result Menu::key_event(tc_ui_document* document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN)
        return TC_UI_EVENT_IGNORED;
    if (event->key == TC_UI_KEY_UP_ARROW || event->key == TC_UI_KEY_DOWN_ARROW) {
        set_current(next_selectable(current_, event->key == TC_UI_KEY_DOWN_ARROW ? 1 : -1));
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_HOME) {
        set_current(next_selectable(SIZE_MAX, 1));
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_END) {
        set_current(next_selectable(SIZE_MAX, -1));
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_ENTER)
        return activate_current(document) ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    if (event->key == TC_UI_KEY_RIGHT) {
        if (open_submenu(document, current_, true))
            return TC_UI_EVENT_HANDLED;
        if (!parent_menu_)
            adjacent_requested_.emit(*this, 1);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_LEFT) {
        if (parent_menu_) {
            Menu* parent = parent_menu_;
            dismiss(document);
            tc_ui_document_set_focus(document, parent->handle());
        } else {
            adjacent_requested_.emit(*this, -1);
        }
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle Menu::hit_test(tc_ui_document* document, float x, float y) {
    const tc_widget_handle own = NativeWidget::hit_test(document, x, y);
    if (!tc_widget_handle_is_invalid(own))
        return own;
    if (!tc_widget_handle_is_invalid(anchor_owner_)) {
        tc_widget* owner = tc_ui_document_resolve_widget(document, anchor_owner_);
        if (owner && rect_contains(owner->bounds, x, y))
            return anchor_owner_;
    }
    return tc_widget_handle_invalid();
}

void Menu::overlay_dismissed(tc_ui_document* document, tc_ui_overlay_dismiss_reason reason) {
    open_ = false;
    close_submenu(document);
    current_ = SIZE_MAX;
    dismissed_.emit(*this, reason);
    if (parent_menu_ && tc_ui_document_is_alive(document, parent_menu_->handle()))
        tc_ui_document_set_focus(document, parent_menu_->handle());
}

void Menu::on_destroy(tc_ui_document* document) {
    close_submenu(document);
    open_ = false;
}

} // namespace termin::gui_native
