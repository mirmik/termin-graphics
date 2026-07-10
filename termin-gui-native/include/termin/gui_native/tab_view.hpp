#pragma once

#include <string>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
struct TabPage { std::string title; tc_widget_handle handle = tc_widget_handle_invalid(); };
class TabView : public NativeWidget {
public:
    explicit TabView(const char* debug_name = nullptr);
    void add_page(std::string title, tc_widget_handle handle);
    void add_page(std::string title, const Widget& widget) { add_page(std::move(title), widget.handle()); }
    bool remove_page(size_t index);
    bool set_page_title(size_t index, std::string title);
    const std::string &page_title(size_t index) const;
    tc_widget_handle page_handle(size_t index) const;
    size_t page_count() const { return child_count(); }
    size_t selected_index() const { return selected_index_; }
    void set_selected_index(size_t index);
    Signal<TabView &, size_t> &selection_changed() {
      return selection_changed_;
    }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;
private:
    tc_ui_rect page_rect() const;
    std::vector<TabPage> pages_;
    size_t selected_index_ = 0;
    float header_height_ = 32.0f;
    float min_tab_width_ = 92.0f;
    Signal<TabView &, size_t> selection_changed_;
};
} // namespace termin::gui_native
