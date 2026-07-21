#include "widgets_internal.hpp"

#include <cmath>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

ToolBar::ToolBar(std::shared_ptr<CommandModel> model)
    : NativeWidget("ToolBar"), model_(model ? std::move(model) : std::make_shared<CommandModel>()) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_cursor_intent(TC_UI_CURSOR_HAND);
    set_preferred_size(tc_ui_size{400.0f, 40.0f});
    observed_revision_ = model_->revision();
    connect_model();
}

ToolBar::~ToolBar() { disconnect_model(); }

void ToolBar::connect_model() {
    if (!model_ || model_connection_ != 0)
        return;
    model_connection_ = model_->changed().connect(
        [this](CommandModel&, const CommandChange& change) { on_model_changed(change); });
}

void ToolBar::disconnect_model() {
    if (model_ && model_connection_ != 0)
        model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void ToolBar::set_model(std::shared_ptr<CommandModel> model) {
    disconnect_model();
    model_ = model ? std::move(model) : std::make_shared<CommandModel>();
    observed_revision_ = model_->revision();
    hovered_ = SIZE_MAX;
    pressed_ = SIZE_MAX;
    item_rects_.clear();
    connect_model();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void ToolBar::on_model_changed(const CommandChange& change) {
    observed_revision_ = model_->revision();
    if (change.kind != CommandChangeKind::Update) {
        hovered_ = SIZE_MAX;
        pressed_ = SIZE_MAX;
    } else if (hovered_ >= model_->size()) {
        hovered_ = SIZE_MAX;
    }
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void ToolBar::sync_model() {
    if (model_->revision() == observed_revision_)
        return;
    observed_revision_ = model_->revision();
    hovered_ = SIZE_MAX;
    pressed_ = SIZE_MAX;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void ToolBar::set_item_height(float height) {
    if (!std::isfinite(height) || height <= 0.0f) {
        tc_log_error("[termin-gui-native] ToolBar rejected invalid item height");
        throw std::invalid_argument("toolbar item height must be finite and positive");
    }
    item_height_ = height;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ToolBar::set_padding(float padding) {
    if (!std::isfinite(padding) || padding < 0.0f) {
        tc_log_error("[termin-gui-native] ToolBar rejected invalid padding");
        throw std::invalid_argument("toolbar padding must be finite and non-negative");
    }
    padding_ = padding;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ToolBar::set_centered(bool centered) {
    if (centered_ == centered)
        return;
    centered_ = centered;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

std::string ToolBar::hovered_tooltip() const {
    if (hovered_ >= model_->size())
        return {};
    return model_->command_at(hovered_).data.tooltip;
}

tc_ui_size ToolBar::measure(tc_ui_document_handle document, tc_ui_constraints constraints) {
    sync_model();
    const tc_ui_style style = computed_style(document);
    float width = padding_ * 2.0f;
    for (const Command& command : model_->commands()) {
        if (command.data.kind == CommandKind::Separator) {
            width += separator_margin_ * 2.0f + separator_width_;
            continue;
        }
        tc_ui_text_metrics label{};
        tc_ui_text_metrics icon{};
        measure_text(document, command.data.label, style.font_size, label);
        measure_text(document, command.data.icon, std::max(18.0f, style.font_size), icon);
        const bool has_icon = command.data.texture_id != 0 || !command.data.icon.empty();
        float item_width =
            command.data.label.empty() ? item_height_ : label.width + padding_ * 2.0f;
        if (has_icon && !command.data.label.empty())
            item_width += item_height_ * 0.55f + icon_gap_;
        width += std::max(item_height_, item_width) + padding_;
    }
    return clamp_size(tc_ui_size{std::max(width, style.min_width),
                                 std::max(item_height_ + padding_ * 2.0f, style.min_height)},
                      constraints);
}

void ToolBar::compute_item_rects(tc_ui_document_handle document) {
    item_rects_.clear();
    item_rects_.reserve(model_->size());
    const tc_ui_style style = computed_style(document);
    float x = bounds().x + padding_;
    const float y = bounds().y + (bounds().height - item_height_) * 0.5f;
    for (const Command& command : model_->commands()) {
        float width = separator_margin_ * 2.0f + separator_width_;
        if (command.data.kind == CommandKind::Action) {
            tc_ui_text_metrics label{};
            measure_text(document, command.data.label, style.font_size, label);
            const bool has_icon = command.data.texture_id != 0 || !command.data.icon.empty();
            width = command.data.label.empty() ? item_height_ : label.width + padding_ * 2.0f;
            if (has_icon && !command.data.label.empty())
                width += item_height_ * 0.55f + icon_gap_;
            width = std::max(item_height_, width);
        }
        item_rects_.push_back(tc_ui_rect{x, y, width, item_height_});
        x += width + (command.data.kind == CommandKind::Action ? padding_ : 0.0f);
    }
    if (centered_ && !item_rects_.empty()) {
        const float content_left = item_rects_.front().x;
        const tc_ui_rect& last_rect = item_rects_.back();
        const float content_right = last_rect.x + last_rect.width;
        const float offset = bounds().x + (bounds().width - (content_right - content_left)) * 0.5f -
                             content_left;
        for (tc_ui_rect& item_rect : item_rects_)
            item_rect.x += offset;
    }
}

void ToolBar::layout(tc_ui_document_handle document, tc_ui_rect rect) {
    sync_model();
    NativeWidget::layout(document, rect);
    compute_item_rects(document);
}

void ToolBar::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
    sync_model();
    if (item_rects_.size() != model_->size())
        compute_item_rects(document);
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_push_clip(context, bounds());
    for (size_t index = 0; index < model_->size(); ++index) {
        const CommandData& command = model_->command_at(index).data;
        const tc_ui_rect rect = item_rects_[index];
        if (command.kind == CommandKind::Separator) {
            const float x = rect.x + separator_margin_;
            tc_ui_painter_draw_line(context, tc_ui_point{x, bounds().y + padding_ * 2.0f},
                                    tc_ui_point{x, bounds().y + bounds().height - padding_ * 2.0f},
                                    style.border, separator_width_);
            continue;
        }
        if (index == hovered_ || index == pressed_ || command.checked) {
            tc_ui_color highlight = style.accent;
            highlight.a *= index == pressed_ ? 0.48f : (command.checked ? 0.36f : 0.20f);
            tc_ui_painter_fill_rounded_rect(context, rect, 4.0f, highlight);
        }
        tc_ui_color foreground = style.foreground;
        if (!command.enabled)
            foreground.a *= 0.45f;
        const bool has_icon = command.texture_id != 0 || !command.icon.empty();
        const float icon_extent = rect.height * 0.55f;
        float text_x = rect.x + padding_;
        if (has_icon) {
            const tc_ui_rect icon_rect{rect.x + padding_,
                                       rect.y + (rect.height - icon_extent) * 0.5f, icon_extent,
                                       icon_extent};
            if (command.texture_id != 0) {
                tc_ui_painter_draw_texture(context, command.texture_id, icon_rect, foreground,
                                           TC_UI_TEXTURE_SAMPLING_LINEAR, false);
            } else {
                tc_ui_text_metrics metrics{};
                const float icon_size = std::max(18.0f, style.font_size);
                measure_text(document, command.icon, icon_size, metrics);
                tc_ui_painter_draw_text(
                    context, command.icon.c_str(),
                    tc_ui_point{icon_rect.x + (icon_rect.width - metrics.width) * 0.5f,
                                rect.y + rect.height * 0.68f},
                    icon_size, foreground);
            }
            text_x = icon_rect.x + icon_rect.width + icon_gap_;
        }
        if (!command.label.empty()) {
            tc_ui_painter_draw_text(context, command.label.c_str(),
                                    tc_ui_point{text_x, rect.y + rect.height * 0.68f},
                                    style.font_size, foreground);
        }
    }
    tc_ui_painter_pop_clip(context);
}

size_t ToolBar::index_at(float x, float y) const {
    for (size_t index = 0; index < item_rects_.size(); ++index) {
        if (model_->command_at(index).data.kind == CommandKind::Action &&
            rect_contains(item_rects_[index], x, y)) {
            return index;
        }
    }
    return SIZE_MAX;
}

bool ToolBar::activate(size_t index) {
    if (index >= model_->size())
        return false;
    const Command& command = model_->command_at(index);
    if (command.data.kind != CommandKind::Action || !command.data.enabled)
        return false;
    if (command.data.checkable)
        model_->set_checked(command.id, !command.data.checked);
    const Command& updated = model_->command(command.id);
    activated_.emit(*this, index, updated.id, updated.data);
    return true;
}

tc_ui_event_result ToolBar::pointer_event(tc_ui_document_handle document,
                                          const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    sync_model();
    if (event->type == TC_UI_POINTER_LEAVE && pressed_ == SIZE_MAX) {
        hovered_ = SIZE_MAX;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_MOVE) {
        const size_t next = index_at(event->x, event->y);
        if (next != hovered_) {
            hovered_ = next;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return rect_contains(bounds(), event->x, event->y) ? TC_UI_EVENT_HANDLED
                                                           : TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN &&
        event->button == tcbase::mouse_button_value(tcbase::MouseButton::LEFT)) {
        const size_t index = index_at(event->x, event->y);
        if (index == SIZE_MAX || !model_->command_at(index).data.enabled) {
            return TC_UI_EVENT_IGNORED;
        }
        pressed_ = index;
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && pressed_ != SIZE_MAX) {
        const size_t pressed = pressed_;
        pressed_ = SIZE_MAX;
        tc_ui_document_release_pointer_capture(document, handle());
        const bool inside = index_at(event->x, event->y) == pressed;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        if (inside)
            activate(pressed);
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

} // namespace termin::gui_native
