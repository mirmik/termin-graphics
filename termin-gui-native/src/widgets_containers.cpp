#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
GroupBox::GroupBox(std::string title, const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "GroupBox"),
      title_(std::move(title)) {
    set_style_role(TC_UI_STYLE_GROUP_BOX);
    set_preferred_size(tc_ui_size {240.0f, 140.0f});
}

GroupBox& GroupBox::set_title(std::string title) {
    title_ = std::move(title);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

GroupBox& GroupBox::set_padding(EdgeInsets padding) {
    tc_ui_style_override style_override = this->style_override();
    style_override.fields |= TC_UI_STYLE_PADDING_LEFT | TC_UI_STYLE_PADDING_TOP |
        TC_UI_STYLE_PADDING_RIGHT | TC_UI_STYLE_PADDING_BOTTOM;
    style_override.value.padding_left = std::max(0.0f, padding.left);
    style_override.value.padding_top = std::max(0.0f, padding.top);
    style_override.value.padding_right = std::max(0.0f, padding.right);
    style_override.value.padding_bottom = std::max(0.0f, padding.bottom);
    if (!set_style_override(style_override)) {
        throw std::runtime_error("failed to set GroupBox padding style override");
    }
    return *this;
}

GroupBox& GroupBox::set_background(Color color) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, color.c_color());
    return *this;
}

GroupBox& GroupBox::set_border(Color color, float thickness) {
    set_style_color(*this, TC_UI_STYLE_BORDER, color.c_color());
    set_style_metric(*this, TC_UI_STYLE_BORDER_WIDTH, std::max(0.0f, thickness));
    return *this;
}

void GroupBox::set_content(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid GroupBox content handle");
        return;
    }
    const tc_widget_handle previous = this->content();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (!attach_child(c_widget(), handle, 0, "GroupBox::set_content")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size GroupBox::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    tc_ui_size measured = preferred_size();
    tc_ui_text_metrics title_metrics {};
    if (measure_text(document, title_, style.font_size, title_metrics)) {
        measured.width = std::max(
            measured.width,
            title_metrics.width + style.padding_left + style.padding_right
        );
    }
    if (!tc_widget_handle_is_invalid(this->content())) {
        if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::measure")) {
            const tc_ui_size content_size = measure_widget(content, document, unconstrained());
            measured.width = std::max(
                measured.width,
                content_size.width + style.padding_left + style.padding_right
            );
            measured.height = std::max(
                measured.height,
                content_size.height + header_height_ + style.padding_top + style.padding_bottom
            );
        }
    }
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void GroupBox::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (tc_widget_handle_is_invalid(this->content())) {
        return;
    }
    tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::layout");
    layout_widget(content, document, content_rect(document));
}

void GroupBox::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    if (color_visible(style.background)) {
        tc_ui_painter_fill_rect(context, bounds(), style.background);
    }
    if (color_visible(style.border) && style.border_width > 0.0f) {
        tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {bounds().x, bounds().y + header_height_},
            tc_ui_point {bounds().x + bounds().width, bounds().y + header_height_},
            style.border,
            style.border_width
        );
    }
    if (!title_.empty()) {
        tc_ui_rect title_clip {
            bounds().x + style.padding_left,
            bounds().y,
            std::max(0.0f, bounds().width - style.padding_left - style.padding_right),
            header_height_
        };
        tc_ui_painter_push_clip(context, title_clip);
        tc_ui_painter_draw_text(
            context,
            title_.c_str(),
            tc_ui_point {
                bounds().x + style.padding_left,
                centered_text_baseline(document, title_, style.font_size, title_clip)
            },
            style.font_size,
            style.foreground
        );
        tc_ui_painter_pop_clip(context);
    }

    const tc_ui_rect clip = content_rect(document);
    tc_ui_painter_push_clip(context, clip);
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::paint");
        paint_widget(content, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result GroupBox::pointer_event(tc_ui_document*, const tc_ui_pointer_event*) {
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle GroupBox::hit_test(tc_ui_document* document, float x, float y) {
    if (!visible() || !rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(this->content()) && rect_contains(content_rect(document), x, y)) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "GroupBox::hit_test");
        if (content && content->vtable && content->vtable->hit_test) {
            tc_widget_handle hit = content->vtable->hit_test(content, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

tc_ui_rect GroupBox::content_rect(tc_ui_document* document) const {
    const tc_ui_style style = computed_style(document);
    tc_ui_rect rect {
        bounds().x + style.padding_left,
        bounds().y + header_height_ + style.padding_top,
        std::max(0.0f, bounds().width - style.padding_left - style.padding_right),
        std::max(0.0f, bounds().height - header_height_ - style.padding_top - style.padding_bottom)
    };
    return rect;
}

Splitter::Splitter(Orientation orientation, const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "Splitter"),
      orientation_(orientation) {
    set_preferred_size(orientation_ == Orientation::Horizontal
        ? tc_ui_size {320.0f, 180.0f}
        : tc_ui_size {240.0f, 260.0f});
}

void Splitter::set_first(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid Splitter first handle");
        return;
    }
    const tc_widget_handle previous = this->first();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (tc_widget_handle_eq(this->second(), handle)) {
        tc_log_error("[termin-gui-native] Splitter first and second widgets must be distinct");
        return;
    }
    if (!attach_child(c_widget(), handle, 0, "Splitter::set_first")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void Splitter::set_second(tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot set invalid Splitter second handle");
        return;
    }
    const tc_widget_handle previous = this->second();
    if (tc_widget_handle_eq(previous, handle)) {
        return;
    }
    if (tc_widget_handle_eq(this->first(), handle)) {
        tc_log_error("[termin-gui-native] Splitter first and second widgets must be distinct");
        return;
    }
    if (!attach_child(c_widget(), handle, 1, "Splitter::set_second")) {
        return;
    }
    detach_if_child(c_widget(), previous);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

Splitter& Splitter::set_split_fraction(float fraction) {
    split_fraction_ = clamp_float(fraction, 0.05f, 0.95f);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT | TC_WIDGET_DIRTY_STATE);
    return *this;
}

Splitter& Splitter::set_min_extents(float first_min, float second_min) {
    first_min_extent_ = std::max(0.0f, first_min);
    second_min_extent_ = std::max(0.0f, second_min);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Splitter& Splitter::set_divider_thickness(float thickness) {
    divider_thickness_ = std::max(1.0f, thickness);
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    return *this;
}

tc_ui_size Splitter::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size first_size {0.0f, 0.0f};
    tc_ui_size second_size {0.0f, 0.0f};
    if (!tc_widget_handle_is_invalid(this->first())) {
        if (tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::measure(first)")) {
            first_size = measure_widget(first, document, unconstrained());
        }
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        if (tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::measure(second)")) {
            second_size = measure_widget(second, document, unconstrained());
        }
    }

    tc_ui_size measured {};
    if (orientation_ == Orientation::Horizontal) {
        measured.width = first_size.width + second_size.width + divider_thickness_;
        measured.height = std::max(first_size.height, second_size.height);
    } else {
        measured.width = std::max(first_size.width, second_size.width);
        measured.height = first_size.height + second_size.height + divider_thickness_;
    }
    measured.width = std::max({measured.width, preferred_size().width, min_size().width});
    measured.height = std::max({measured.height, preferred_size().height, min_size().height});
    return clamp_size(measured, constraints);
}

void Splitter::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    layout_children(document);
}

void Splitter::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    if (!tc_widget_handle_is_invalid(this->first())) {
        tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::paint(first)");
        paint_widget(first, document, context);
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::paint(second)");
        paint_widget(second, document, context);
    }
    tc_ui_painter_pop_clip(context);
    tc_ui_painter_fill_rect(context, divider_rect(), divider_color_.c_color());
}

tc_ui_event_result Splitter::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (!captured && !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(divider_rect(), event->x, event->y)) {
        tc_ui_document_set_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && captured) {
        const float axis = split_axis_extent();
        if (axis > 0.0f) {
            const float position = orientation_ == Orientation::Horizontal
                ? event->x - bounds().x
                : event->y - bounds().y;
            set_split_fraction(position / axis);
            layout_children(document);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && captured) {
        tc_ui_document_release_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }

    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle Splitter::hit_test(tc_ui_document* document, float x, float y) {
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (rect_contains(divider_rect(), x, y)) {
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }
    auto hit_child = [this, document, x, y](tc_widget_handle handle) {
        tc_widget* child = resolve_child(document, c_widget(), handle, "Splitter::hit_test");
        if (!child || !child->vtable || !child->vtable->hit_test) {
            return tc_widget_handle_invalid();
        }
        return child->vtable->hit_test(child, document, x, y);
    };
    tc_widget_handle second_hit = hit_child(this->second());
    if (!tc_widget_handle_is_invalid(second_hit)) {
        return second_hit;
    }
    tc_widget_handle first_hit = hit_child(this->first());
    if (!tc_widget_handle_is_invalid(first_hit)) {
        return first_hit;
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

tc_ui_rect Splitter::divider_rect() const {
    const float axis = split_axis_extent();
    if (orientation_ == Orientation::Horizontal) {
        const float first_extent = clamp_float(
            axis * split_fraction_,
            first_min_extent_,
            std::max(first_min_extent_, axis - second_min_extent_)
        );
        return tc_ui_rect {bounds().x + first_extent, bounds().y, divider_thickness_, bounds().height};
    }
    const float first_extent = clamp_float(
        axis * split_fraction_,
        first_min_extent_,
        std::max(first_min_extent_, axis - second_min_extent_)
    );
    return tc_ui_rect {bounds().x, bounds().y + first_extent, bounds().width, divider_thickness_};
}

void Splitter::layout_children(tc_ui_document* document) {
    const float axis = split_axis_extent();
    const float first_extent = clamp_float(
        axis * split_fraction_,
        first_min_extent_,
        std::max(first_min_extent_, axis - second_min_extent_)
    );
    const float second_extent = std::max(0.0f, axis - first_extent);
    if (!tc_widget_handle_is_invalid(this->first())) {
        tc_widget* first = resolve_child(document, c_widget(), this->first(), "Splitter::layout(first)");
        tc_ui_rect first_rect = orientation_ == Orientation::Horizontal
            ? tc_ui_rect {bounds().x, bounds().y, first_extent, bounds().height}
            : tc_ui_rect {bounds().x, bounds().y, bounds().width, first_extent};
        layout_widget(first, document, first_rect);
    }
    if (!tc_widget_handle_is_invalid(this->second())) {
        tc_widget* second = resolve_child(document, c_widget(), this->second(), "Splitter::layout(second)");
        tc_ui_rect second_rect = orientation_ == Orientation::Horizontal
            ? tc_ui_rect {
                bounds().x + first_extent + divider_thickness_,
                bounds().y,
                second_extent,
                bounds().height
            }
            : tc_ui_rect {
                bounds().x,
                bounds().y + first_extent + divider_thickness_,
                bounds().width,
                second_extent
            };
        layout_widget(second, document, second_rect);
    }
}

float Splitter::split_axis_extent() const {
    const float axis = orientation_ == Orientation::Horizontal ? bounds().width : bounds().height;
    return std::max(0.0f, axis - divider_thickness_);
}

ScrollArea::ScrollArea(const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "ScrollArea") {
    set_preferred_size(tc_ui_size {240.0f, 180.0f});
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
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void ScrollArea::set_scroll(float x, float y) {
    scroll_x_ = std::max(0.0f, x);
    scroll_y_ = std::max(0.0f, y);
    clamp_scroll();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size ScrollArea::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size measured = preferred_size();
    if (!tc_widget_handle_is_invalid(this->content())) {
        if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::measure")) {
            tc_ui_size content_size = measure_widget(content, document, unconstrained());
            measured.width = std::max(measured.width, std::min(content_size.width, preferred_size().width));
            measured.height = std::max(measured.height, std::min(content_size.height, preferred_size().height));
        }
    }
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void ScrollArea::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    content_size_ = tc_ui_size {0.0f, 0.0f};
    if (tc_widget_handle_is_invalid(this->content())) {
        return;
    }
    tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::layout");
    if (!content) {
        return;
    }

    tc_ui_size measured = measure_widget(content, document, unconstrained());
    content_size_ = tc_ui_size {
        std::max(measured.width, rect.width),
        std::max(measured.height, rect.height)
    };
    clamp_scroll();
    layout_widget(
        content,
        document,
        tc_ui_rect {rect.x - scroll_x_, rect.y - scroll_y_, content_size_.width, content_size_.height}
    );
}

void ScrollArea::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    tc_ui_painter_push_clip(context, bounds());
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::paint");
        paint_widget(content, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result ScrollArea::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_WHEEL) {
        const float delta_y = event->wheel_y != 0.0f ? -event->wheel_y * wheel_step_ : 0.0f;
        const float delta_x = event->wheel_x != 0.0f ? -event->wheel_x * wheel_step_ : 0.0f;
        set_scroll(scroll_x_ + delta_x, scroll_y_ + delta_y);
        if (!tc_widget_handle_is_invalid(this->content())) {
            if (tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::wheel")) {
                layout_widget(
                    content,
                    document,
                    tc_ui_rect {
                        bounds().x - scroll_x_,
                        bounds().y - scroll_y_,
                        content_size_.width,
                        content_size_.height
                    }
                );
            }
        }
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle ScrollArea::hit_test(tc_ui_document* document, float x, float y) {
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(this->content())) {
        tc_widget* content = resolve_child(document, c_widget(), this->content(), "ScrollArea::hit_test");
        if (content && content->vtable && content->vtable->hit_test) {
            tc_widget_handle hit = content->vtable->hit_test(content, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

void ScrollArea::clamp_scroll() {
    scroll_x_ = clamp_float(scroll_x_, 0.0f, std::max(0.0f, content_size_.width - bounds().width));
    scroll_y_ = clamp_float(scroll_y_, 0.0f, std::max(0.0f, content_size_.height - bounds().height));
}

TabView::TabView(const char* debug_name)
    : NativeWidget(debug_name ? debug_name : "TabView") {
    set_style_role(TC_UI_STYLE_TAB);
    set_preferred_size(tc_ui_size {320.0f, 220.0f});
}

void TabView::add_page(std::string title, tc_widget_handle handle) {
    if (tc_widget_handle_is_invalid(handle)) {
        tc_log_error("[termin-gui-native] cannot add invalid TabView page handle");
        return;
    }
    const auto duplicate = std::find_if(
        pages_.begin(),
        pages_.end(),
        [handle](const TabPage& page) { return tc_widget_handle_eq(page.handle, handle); }
    );
    if (duplicate != pages_.end()) {
        tc_log_error("[termin-gui-native] cannot add the same widget to TabView twice");
        return;
    }
    if (!attach_child(c_widget(), handle, SIZE_MAX, "TabView::add_page")) {
        return;
    }
    pages_.push_back(TabPage {std::move(title), handle});
    if (child_count() == 1) {
        selected_index_ = 0;
    }
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TabView::set_selected_index(size_t index) {
    if (index >= child_count() || selected_index_ == index) {
        return;
    }
    selected_index_ = index;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size TabView::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    tc_ui_size content {preferred_size().width, std::max(0.0f, preferred_size().height - header_height_)};
    for (size_t index = 0; index < child_count(); ++index) {
        tc_widget* child = child_at(index);
        if (!child || !tc_widget_is_visible(child)) {
            continue;
        }
        tc_ui_size child_size = measure_widget(child, document, unconstrained());
        content.width = std::max(content.width, child_size.width);
        content.height = std::max(content.height, child_size.height);
    }
    return clamp_size(tc_ui_size {content.width, content.height + header_height_}, constraints);
}

void TabView::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    if (child_count() == 0 || selected_index_ >= child_count()) {
        return;
    }
    tc_widget* selected = child_at(selected_index_);
    layout_widget(selected, document, page_rect());
}

void TabView::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const tc_ui_style selected_style = computed_style(document, TC_UI_STYLE_STATE_CHECKED);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    const float tab_width = child_count() == 0
        ? min_tab_width_
        : std::max(min_tab_width_, bounds().width / static_cast<float>(child_count()));
    for (size_t i = 0; i < child_count(); ++i) {
        const tc_widget* child = child_at(i);
        const TabPage* page = child ? find_tab_page(pages_, child->handle) : nullptr;
        tc_ui_rect tab {
            bounds().x + tab_width * static_cast<float>(i),
            bounds().y,
            tab_width,
            header_height_
        };
        const bool selected = i == selected_index_;
        tc_ui_painter_fill_rect(
            context,
            tab,
            selected ? selected_style.background : style.background
        );
        tc_ui_painter_stroke_rect(context, tab, style.border, style.border_width);
        tc_ui_painter_push_clip(context, tab);
        tc_ui_painter_draw_text(
            context,
            page ? page->title.c_str() : "",
            tc_ui_point {
                tab.x + style.padding_left,
                centered_text_baseline(
                    document,
                    page ? std::string_view(page->title) : std::string_view {},
                    style.font_size,
                    tab
                )
            },
            style.font_size,
            style.foreground
        );
        tc_ui_painter_pop_clip(context);
    }

    tc_ui_rect body = page_rect();
    tc_ui_painter_push_clip(context, body);
    if (child_count() > 0 && selected_index_ < child_count()) {
        tc_widget* selected = child_at(selected_index_);
        paint_widget(selected, document, context);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result TabView::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event || !rect_contains(bounds(), event->x, event->y)) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN && event->y < bounds().y + header_height_ && child_count() > 0) {
        const float tab_width = std::max(min_tab_width_, bounds().width / static_cast<float>(child_count()));
        const size_t index = static_cast<size_t>(std::max(0.0f, (event->x - bounds().x) / tab_width));
        if (index < child_count()) {
            set_selected_index(index);
            if (tc_widget* selected = child_at(selected_index_)) {
                layout_widget(selected, document, page_rect());
            }
            return TC_UI_EVENT_HANDLED;
        }
    }
    return TC_UI_EVENT_IGNORED;
}

tc_widget_handle TabView::hit_test(tc_ui_document* document, float x, float y) {
    if (!rect_contains(bounds(), x, y)) {
        return tc_widget_handle_invalid();
    }
    if (y < bounds().y + header_height_) {
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }
    if (child_count() > 0 && selected_index_ < child_count() && rect_contains(page_rect(), x, y)) {
        tc_widget* selected = child_at(selected_index_);
        if (selected && selected->vtable && selected->vtable->hit_test) {
            tc_widget_handle hit = selected->vtable->hit_test(selected, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
    }
    return mouse_transparent() ? tc_widget_handle_invalid() : handle();
}

tc_ui_rect TabView::page_rect() const {
    return tc_ui_rect {
        bounds().x,
        bounds().y + header_height_,
        bounds().width,
        std::max(0.0f, bounds().height - header_height_)
    };
}

Panel::Panel(const char* debug_name)
    : NativeWidget(debug_name) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_preferred_size(tc_ui_size {96.0f, 64.0f});
}

Panel& Panel::set_fill(Color color) {
    set_style_color(*this, TC_UI_STYLE_BACKGROUND, color.c_color());
    return *this;
}

Panel& Panel::set_border(Color color, float thickness) {
    set_style_color(*this, TC_UI_STYLE_BORDER, color.c_color());
    set_style_metric(*this, TC_UI_STYLE_BORDER_WIDTH, std::max(0.0f, thickness));
    return *this;
}

void Panel::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    if (style.border_width > 0.0f && color_visible(style.border)) {
        tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
    }
}


} // namespace termin::gui_native
