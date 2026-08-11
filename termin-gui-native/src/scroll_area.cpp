#include "widgets_internal.hpp"

namespace termin::gui_native {
    using namespace detail;

    namespace {

        constexpr float SCROLLBAR_THICKNESS = 10.0f;
        constexpr float SCROLLBAR_INSET = 2.0f;
        constexpr float MIN_THUMB_EXTENT = 20.0f;
        constexpr float SCROLL_EPSILON = 0.0001f;

        bool different(float left, float right) {
            return std::fabs(left - right) > SCROLL_EPSILON;
        }

    } // namespace

    ScrollArea::ScrollArea(const char* debug_name)
        : NativeWidget(debug_name ? debug_name : "ScrollArea") {
        set_focusable(true);
        set_preferred_size(tc_ui_size{240.0f, 180.0f});
    }

    void ScrollArea::set_content(tc_widget_handle handle) {
        if (tc_widget_handle_is_invalid(handle)) {
            tc_log_error("[termin-gui-native] cannot set invalid ScrollArea content handle");
            return;
        }
        const tc_widget_handle previous = this->content();
        if (tc_widget_handle_eq(previous, handle)) {
            return;
        }
        if (!attach_child(c_widget(), handle, 0, "ScrollArea::set_content")) {
            return;
        }
        detach_if_child(c_widget(), previous);
        content_size_ = tc_ui_size{0.0f, 0.0f};
        apply_scroll(document(), 0.0f, 0.0f);
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }

    void ScrollArea::set_scroll_axes(bool horizontal, bool vertical) {
        horizontal_scroll_enabled_ = horizontal;
        vertical_scroll_enabled_ = vertical;
        apply_scroll(document(), scroll_x_, scroll_y_);
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    }

    void ScrollArea::set_scrollbar_policy(ScrollBarPolicy horizontal, ScrollBarPolicy vertical) {
        if (horizontal_scrollbar_policy_ == horizontal && vertical_scrollbar_policy_ == vertical) {
            return;
        }
        horizontal_scrollbar_policy_ = horizontal;
        vertical_scrollbar_policy_ = vertical;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }

    bool ScrollArea::horizontal_scrollbar_visible() const {
        if (!horizontal_scroll_enabled_ || horizontal_scrollbar_policy_ == ScrollBarPolicy::Hidden) {
            return false;
        }
        return horizontal_scrollbar_policy_ == ScrollBarPolicy::Always || max_scroll_x() > SCROLL_EPSILON;
    }

    bool ScrollArea::vertical_scrollbar_visible() const {
        if (!vertical_scroll_enabled_ || vertical_scrollbar_policy_ == ScrollBarPolicy::Hidden) {
            return false;
        }
        return vertical_scrollbar_policy_ == ScrollBarPolicy::Always || max_scroll_y() > SCROLL_EPSILON;
    }

    void ScrollArea::set_scroll(float x, float y) {
        apply_scroll(document(), x, y);
    }

    float ScrollArea::max_scroll_x() const {
        return horizontal_scroll_enabled_ ? std::max(0.0f, content_size_.width - bounds().width) : 0.0f;
    }

    float ScrollArea::max_scroll_y() const {
        return vertical_scroll_enabled_ ? std::max(0.0f, content_size_.height - bounds().height) : 0.0f;
    }

    bool ScrollArea::apply_scroll(tc_ui_document_handle document, float x, float y) {
        const float next_x = horizontal_scroll_enabled_ ? clamp_float(x, 0.0f, max_scroll_x()) : 0.0f;
        const float next_y = vertical_scroll_enabled_ ? clamp_float(y, 0.0f, max_scroll_y()) : 0.0f;
        if (!different(next_x, scroll_x_) && !different(next_y, scroll_y_)) {
            return false;
        }
        scroll_x_ = next_x;
        scroll_y_ = next_y;
        if (!tc_ui_document_handle_is_invalid(document) && tc_ui_document_is_alive(document, handle())) {
            layout_content(document);
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        changed_.emit(*this, scroll_x_, scroll_y_);
        return true;
    }

    tc_ui_size ScrollArea::measure(tc_ui_document_handle document, tc_ui_constraints constraints) {
        tc_ui_size measured = preferred_size();
        if (!tc_widget_handle_is_invalid(this->content())) {
            if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::measure")) {
                const tc_ui_insets margin = tc_widget_layout_spec(content).margin;
                const bool width_definite =
                    constraints.max_size.width > 0.0f && constraints.min_size.width == constraints.max_size.width;
                const bool height_definite =
                    constraints.max_size.height > 0.0f && constraints.min_size.height == constraints.max_size.height;
                tc_ui_constraints content_constraints = unconstrained();
                if (!horizontal_scroll_enabled_ && width_definite) {
                    const float width = std::max(0.0f, constraints.max_size.width - margin.left - margin.right);
                    content_constraints.min_size.width = width;
                    content_constraints.max_size.width = width;
                }
                if (!vertical_scroll_enabled_ && height_definite) {
                    const float height = std::max(0.0f, constraints.max_size.height - margin.top - margin.bottom);
                    content_constraints.min_size.height = height;
                    content_constraints.max_size.height = height;
                }
                const tc_ui_size content_size = measure_widget(
                    content, document, content_constraints, constraints.max_size, width_definite, height_definite);
                measured.width = std::max(
                    measured.width, std::min(content_size.width + margin.left + margin.right, preferred_size().width));
                measured.height =
                    std::max(measured.height,
                             std::min(content_size.height + margin.top + margin.bottom, preferred_size().height));
            }
        }
        measured.width = std::max(measured.width, min_size().width);
        measured.height = std::max(measured.height, min_size().height);
        return clamp_size(measured, constraints);
    }

    void ScrollArea::layout(tc_ui_document_handle document, tc_ui_rect rect) {
        NativeWidget::layout(document, rect);
        content_size_ = tc_ui_size{0.0f, 0.0f};
        if (!tc_widget_handle_is_invalid(this->content())) {
            tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::layout");
            if (content) {
                const tc_ui_insets margin = tc_widget_layout_spec(content).margin;
                tc_ui_constraints content_constraints = unconstrained();
                if (!horizontal_scroll_enabled_) {
                    const float width = std::max(0.0f, rect.width - margin.left - margin.right);
                    content_constraints.min_size.width = width;
                    content_constraints.max_size.width = width;
                }
                if (!vertical_scroll_enabled_) {
                    const float height = std::max(0.0f, rect.height - margin.top - margin.bottom);
                    content_constraints.min_size.height = height;
                    content_constraints.max_size.height = height;
                }
                const tc_ui_size measured = measure_widget(
                    content, document, content_constraints, tc_ui_size{rect.width, rect.height}, true, true);
                content_size_ = tc_ui_size{
                    horizontal_scroll_enabled_ ? std::max(measured.width + margin.left + margin.right, rect.width)
                                               : rect.width,
                    vertical_scroll_enabled_ ? std::max(measured.height + margin.top + margin.bottom, rect.height)
                                             : rect.height};
            }
        }
        const float previous_x = scroll_x_;
        const float previous_y = scroll_y_;
        scroll_x_ = clamp_float(scroll_x_, 0.0f, max_scroll_x());
        scroll_y_ = clamp_float(scroll_y_, 0.0f, max_scroll_y());
        layout_content(document);
        if (different(previous_x, scroll_x_) || different(previous_y, scroll_y_)) {
            changed_.emit(*this, scroll_x_, scroll_y_);
        }
    }

    void ScrollArea::layout_content(tc_ui_document_handle document) {
        if (tc_widget_handle_is_invalid(this->content())) {
            return;
        }
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::layout_content");
        if (!content) {
            return;
        }
        const tc_ui_insets margin = tc_widget_layout_spec(content).margin;
        layout_widget(content,
                      document,
                      tc_ui_rect{bounds().x - scroll_x_ + margin.left,
                                 bounds().y - scroll_y_ + margin.top,
                                 std::max(0.0f, content_size_.width - margin.left - margin.right),
                                 std::max(0.0f, content_size_.height - margin.top - margin.bottom)});
    }

    tc_ui_rect ScrollArea::horizontal_track_rect() const {
        const float vertical_reserve = vertical_scrollbar_visible() ? SCROLLBAR_THICKNESS + SCROLLBAR_INSET : 0.0f;
        return tc_ui_rect{bounds().x + SCROLLBAR_INSET,
                          bounds().y + bounds().height - SCROLLBAR_THICKNESS - SCROLLBAR_INSET,
                          std::max(0.0f, bounds().width - 2.0f * SCROLLBAR_INSET - vertical_reserve),
                          SCROLLBAR_THICKNESS};
    }

    tc_ui_rect ScrollArea::vertical_track_rect() const {
        const float horizontal_reserve = horizontal_scrollbar_visible() ? SCROLLBAR_THICKNESS + SCROLLBAR_INSET : 0.0f;
        return tc_ui_rect{bounds().x + bounds().width - SCROLLBAR_THICKNESS - SCROLLBAR_INSET,
                          bounds().y + SCROLLBAR_INSET,
                          SCROLLBAR_THICKNESS,
                          std::max(0.0f, bounds().height - 2.0f * SCROLLBAR_INSET - horizontal_reserve)};
    }

    tc_ui_rect ScrollArea::horizontal_thumb_rect() const {
        const tc_ui_rect track = horizontal_track_rect();
        const float viewport = bounds().width;
        const float extent =
            content_size_.width > 0.0f
                ? std::min(track.width, std::max(MIN_THUMB_EXTENT, track.width * viewport / content_size_.width))
                : track.width;
        const float travel = std::max(0.0f, track.width - extent);
        const float position = max_scroll_x() > 0.0f ? travel * scroll_x_ / max_scroll_x() : 0.0f;
        return tc_ui_rect{track.x + position, track.y, extent, track.height};
    }

    tc_ui_rect ScrollArea::vertical_thumb_rect() const {
        const tc_ui_rect track = vertical_track_rect();
        const float viewport = bounds().height;
        const float extent =
            content_size_.height > 0.0f
                ? std::min(track.height, std::max(MIN_THUMB_EXTENT, track.height * viewport / content_size_.height))
                : track.height;
        const float travel = std::max(0.0f, track.height - extent);
        const float position = max_scroll_y() > 0.0f ? travel * scroll_y_ / max_scroll_y() : 0.0f;
        return tc_ui_rect{track.x, track.y + position, track.width, extent};
    }

    void ScrollArea::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
        tc_ui_painter_push_clip(context, bounds());
        if (!tc_widget_handle_is_invalid(this->content())) {
            tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::paint");
            paint_widget(content, document, context);
        }
        tc_ui_painter_pop_clip(context);

        const tc_ui_style style = computed_style(document);
        if (horizontal_scrollbar_visible()) {
            tc_ui_painter_fill_rounded_rect(context, horizontal_track_rect(), SCROLLBAR_THICKNESS * 0.5f, style.border);
            tc_ui_painter_fill_rounded_rect(
                context, horizontal_thumb_rect(), SCROLLBAR_THICKNESS * 0.5f, style.foreground);
        }
        if (vertical_scrollbar_visible()) {
            tc_ui_painter_fill_rounded_rect(context, vertical_track_rect(), SCROLLBAR_THICKNESS * 0.5f, style.border);
            tc_ui_painter_fill_rounded_rect(
                context, vertical_thumb_rect(), SCROLLBAR_THICKNESS * 0.5f, style.foreground);
        }
    }

    tc_ui_event_result ScrollArea::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
        if (!event) {
            return TC_UI_EVENT_IGNORED;
        }
        if (event->type == TC_UI_POINTER_CANCEL) {
            const bool was_dragging = drag_axis_ != DragAxis::None;
            drag_axis_ = DragAxis::None;
            return was_dragging ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
        }
        const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
        if (event->type == TC_UI_POINTER_MOVE && (drag_axis_ != DragAxis::None || captured)) {
            if (drag_axis_ == DragAxis::Horizontal) {
                const tc_ui_rect track = horizontal_track_rect();
                const tc_ui_rect thumb = horizontal_thumb_rect();
                const float travel = track.width - thumb.width;
                if (travel > 0.0f) {
                    apply_scroll(document,
                                 drag_scroll_origin_ + (event->x - drag_pointer_origin_) * max_scroll_x() / travel,
                                 scroll_y_);
                }
            } else if (drag_axis_ == DragAxis::Vertical) {
                const tc_ui_rect track = vertical_track_rect();
                const tc_ui_rect thumb = vertical_thumb_rect();
                const float travel = track.height - thumb.height;
                if (travel > 0.0f) {
                    apply_scroll(document,
                                 scroll_x_,
                                 drag_scroll_origin_ + (event->y - drag_pointer_origin_) * max_scroll_y() / travel);
                }
            }
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_UP && (drag_axis_ != DragAxis::None || captured)) {
            drag_axis_ = DragAxis::None;
            if (captured) {
                tc_ui_document_release_pointer_capture(document, handle());
            }
            return TC_UI_EVENT_HANDLED;
        }
        if (!rect_contains(bounds(), event->x, event->y)) {
            return TC_UI_EVENT_IGNORED;
        }
        if (event->type == TC_UI_POINTER_WHEEL) {
            const float delta_x = horizontal_scroll_enabled_ ? -event->wheel_x * wheel_step_ : 0.0f;
            const float delta_y = vertical_scroll_enabled_ ? -event->wheel_y * wheel_step_ : 0.0f;
            return apply_scroll(document, scroll_x_ + delta_x, scroll_y_ + delta_y) ? TC_UI_EVENT_HANDLED
                                                                                    : TC_UI_EVENT_IGNORED;
        }
        if (event->type != TC_UI_POINTER_DOWN ||
            event->button != tcbase::mouse_button_value(tcbase::MouseButton::LEFT)) {
            return TC_UI_EVENT_IGNORED;
        }
        if (horizontal_scrollbar_visible() && rect_contains(horizontal_track_rect(), event->x, event->y)) {
            const tc_ui_rect thumb = horizontal_thumb_rect();
            if (rect_contains(thumb, event->x, event->y)) {
                drag_axis_ = DragAxis::Horizontal;
                drag_pointer_origin_ = event->x;
                drag_scroll_origin_ = scroll_x_;
                tc_ui_document_set_pointer_capture(document, handle());
            } else {
                apply_scroll(document, scroll_x_ + (event->x < thumb.x ? -bounds().width : bounds().width), scroll_y_);
            }
            return TC_UI_EVENT_HANDLED;
        }
        if (vertical_scrollbar_visible() && rect_contains(vertical_track_rect(), event->x, event->y)) {
            const tc_ui_rect thumb = vertical_thumb_rect();
            if (rect_contains(thumb, event->x, event->y)) {
                drag_axis_ = DragAxis::Vertical;
                drag_pointer_origin_ = event->y;
                drag_scroll_origin_ = scroll_y_;
                tc_ui_document_set_pointer_capture(document, handle());
            } else {
                apply_scroll(
                    document, scroll_x_, scroll_y_ + (event->y < thumb.y ? -bounds().height : bounds().height));
            }
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }

    tc_widget_handle ScrollArea::hit_test(tc_ui_document_handle document, float x, float y) {
        if (!rect_contains(bounds(), x, y)) {
            return tc_widget_handle_invalid();
        }
        if ((horizontal_scrollbar_visible() && rect_contains(horizontal_track_rect(), x, y)) ||
            (vertical_scrollbar_visible() && rect_contains(vertical_track_rect(), x, y))) {
            return handle();
        }
        if (!tc_widget_handle_is_invalid(this->content())) {
            tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::hit_test");
            if (content) {
                const tc_widget_handle hit = detail::hit_test_widget(content, document, x, y);
                if (!tc_widget_handle_is_invalid(hit)) {
                    return hit;
                }
            }
        }
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }

    tc_ui_event_result ScrollArea::key_event(tc_ui_document_handle document, const tc_ui_key_event* event) {
        if (!event || event->type != TC_UI_KEY_DOWN) {
            return TC_UI_EVENT_IGNORED;
        }
        float next_x = scroll_x_;
        float next_y = scroll_y_;
        switch (event->key) {
        case TC_UI_KEY_LEFT:
            next_x -= wheel_step_;
            break;
        case TC_UI_KEY_RIGHT:
            next_x += wheel_step_;
            break;
        case TC_UI_KEY_UP_ARROW:
            next_y -= wheel_step_;
            break;
        case TC_UI_KEY_DOWN_ARROW:
            next_y += wheel_step_;
            break;
        case TC_UI_KEY_PAGE_UP:
            if (vertical_scroll_enabled_) {
                next_y -= bounds().height;
            } else {
                next_x -= bounds().width;
            }
            break;
        case TC_UI_KEY_PAGE_DOWN:
            if (vertical_scroll_enabled_) {
                next_y += bounds().height;
            } else {
                next_x += bounds().width;
            }
            break;
        case TC_UI_KEY_HOME:
            if (vertical_scroll_enabled_) {
                next_y = 0.0f;
            } else {
                next_x = 0.0f;
            }
            break;
        case TC_UI_KEY_END:
            if (vertical_scroll_enabled_) {
                next_y = max_scroll_y();
            } else {
                next_x = max_scroll_x();
            }
            break;
        default:
            return TC_UI_EVENT_IGNORED;
        }
        return apply_scroll(document, next_x, next_y) ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }

    bool ScrollArea::ensure_visible(tc_widget_handle descendant) {
        const tc_ui_document_handle current_document = document();
        if (tc_ui_document_handle_is_invalid(current_document) ||
            !tc_ui_document_is_alive(current_document, descendant)) {
            tc_log_error("[termin-gui-native] ScrollArea cannot reveal invalid or stale descendant");
            return false;
        }
        tc_widget* target = tc_ui_document_resolve_widget(current_document, descendant);
        bool belongs = false;
        for (tc_widget* ancestor = target; ancestor; ancestor = ancestor->parent) {
            if (ancestor == c_widget()) {
                belongs = true;
                break;
            }
        }
        if (!belongs || target == c_widget()) {
            tc_log_error("[termin-gui-native] ScrollArea cannot reveal a non-descendant widget");
            return false;
        }

        const tc_ui_rect target_rect = tc_widget_bounds(target);
        float next_x = scroll_x_;
        float next_y = scroll_y_;
        if (horizontal_scroll_enabled_) {
            if (target_rect.x < bounds().x) {
                next_x += target_rect.x - bounds().x;
            } else if (target_rect.x + target_rect.width > bounds().x + bounds().width) {
                next_x += target_rect.x + target_rect.width - (bounds().x + bounds().width);
            }
        }
        if (vertical_scroll_enabled_) {
            if (target_rect.y < bounds().y) {
                next_y += target_rect.y - bounds().y;
            } else if (target_rect.y + target_rect.height > bounds().y + bounds().height) {
                next_y += target_rect.y + target_rect.height - (bounds().y + bounds().height);
            }
        }
        return apply_scroll(current_document, next_x, next_y);
    }

    void ScrollArea::descendant_focused(tc_ui_document_handle, tc_widget_handle descendant) {
        ensure_visible(descendant);
    }

} // namespace termin::gui_native
