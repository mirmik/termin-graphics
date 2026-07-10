#include "widgets_internal.hpp"

#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

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

bool TabView::remove_page(size_t index) {
  if (index >= pages_.size() || index >= child_count()) {
    tc_log_error("[termin-gui-native] TabView remove_page index out of range");
    return false;
  }
  const tc_widget_handle handle = pages_[index].handle;
  const bool removed_selected = selected_index_ == index;
  detach_if_child(c_widget(), handle);
  pages_.erase(pages_.begin() + static_cast<std::ptrdiff_t>(index));
  const size_t previous = selected_index_;
  if (pages_.empty()) {
    selected_index_ = 0;
  } else if (selected_index_ > index) {
    --selected_index_;
  } else if (selected_index_ >= pages_.size()) {
    selected_index_ = pages_.size() - 1;
  }
  mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_LAYOUT |
             TC_WIDGET_DIRTY_PAINT);
  if (!pages_.empty() && (selected_index_ != previous || removed_selected)) {
    selection_changed_.emit(*this, selected_index_);
  }
  return true;
}

bool TabView::set_page_title(size_t index, std::string title) {
  if (index >= pages_.size()) {
    tc_log_error(
        "[termin-gui-native] TabView set_page_title index out of range");
    return false;
  }
  pages_[index].title = std::move(title);
  mark_dirty(TC_WIDGET_DIRTY_PAINT);
  return true;
}

const std::string &TabView::page_title(size_t index) const {
  if (index >= pages_.size()) {
    throw std::out_of_range("TabView page_title index out of range");
  }
  return pages_[index].title;
}

tc_widget_handle TabView::page_handle(size_t index) const {
  if (index >= pages_.size()) {
    return tc_widget_handle_invalid();
  }
  return pages_[index].handle;
}

void TabView::set_selected_index(size_t index) {
    if (index >= child_count() || selected_index_ == index) {
        return;
    }
    selected_index_ = index;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    selection_changed_.emit(*this, selected_index_);
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
    for (size_t i = 0; i < child_count(); ++i) {
        const tc_widget* child = child_at(i);
        const TabPage* page = child ? find_tab_page(pages_, child->handle) : nullptr;
        const tc_ui_rect tab = tab_rect(document, i);
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
        for (size_t index = 0; index < child_count(); ++index) {
            if (!rect_contains(tab_rect(document, index), event->x, event->y)) {
                continue;
            }
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

float TabView::tab_width(tc_ui_document* document, size_t index) const {
    if (index >= pages_.size()) {
        return min_tab_width_;
    }
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    const std::string& title = pages_[index].title;
    if (!measure_text(document, title, style.font_size, metrics)) {
        return min_tab_width_;
    }
    return std::max(
        min_tab_width_,
        metrics.width + style.padding_left + style.padding_right + 12.0f
    );
}

tc_ui_rect TabView::tab_rect(tc_ui_document* document, size_t index) const {
    float x = bounds().x;
    for (size_t current = 0; current < index; ++current) {
        x += tab_width(document, current);
    }
    return tc_ui_rect {x, bounds().y, tab_width(document, index), header_height_};
}


} // namespace termin::gui_native
