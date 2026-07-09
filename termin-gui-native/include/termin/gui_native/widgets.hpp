#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <termin/gui_native/widget.hpp>

namespace termin::gui_native {

template<typename... Args>
class Signal {
public:
    using Callback = std::function<void(Args...)>;

    size_t connect(Callback callback) {
        if (!callback) {
            return 0;
        }
        const size_t id = next_id_++;
        slots_.push_back(Slot {id, std::move(callback)});
        return id;
    }

    bool disconnect(size_t id) {
        if (id == 0) {
            return false;
        }
        const auto before = slots_.size();
        slots_.erase(
            std::remove_if(
                slots_.begin(),
                slots_.end(),
                [id](const Slot& slot) { return slot.id == id; }
            ),
            slots_.end()
        );
        return slots_.size() != before;
    }

    void emit(Args... args) const {
        for (const Slot& slot : slots_) {
            slot.callback(args...);
        }
    }

    size_t size() const { return slots_.size(); }

private:
    struct Slot {
        size_t id = 0;
        Callback callback;
    };

    size_t next_id_ = 1;
    std::vector<Slot> slots_;
};

enum class Orientation {
    Horizontal,
    Vertical,
};

struct EdgeInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class LayoutPolicy {
    Fixed,
    Preferred,
    Flex,
    Stretch,
};

struct LayoutItem {
    tc_widget_handle handle {};
    LayoutPolicy policy = LayoutPolicy::Stretch;
    float fixed_extent = 0.0f;
    float flex = 1.0f;
    float grow = 1.0f;
    float shrink = 1.0f;
    float min_extent = 0.0f;
    float max_extent = 0.0f;
};

struct GridTrack {
    LayoutPolicy policy = LayoutPolicy::Stretch;
    float value = 0.0f;
    float grow = 1.0f;
    float shrink = 1.0f;
    float min_extent = 0.0f;
    float max_extent = 0.0f;
};

struct GridItem {
    tc_widget_handle handle {};
    size_t row = 0;
    size_t column = 0;
    size_t row_span = 1;
    size_t column_span = 1;
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    tc_ui_color c_color() const {
        return tc_ui_color {r, g, b, a};
    }
};

class NativeWidget : public Widget {
public:
    explicit NativeWidget(const char* debug_name = nullptr);
    ~NativeWidget() override = default;

    tc_ui_rect bounds() const { return tc_widget_bounds(c_widget()); }
    void set_min_size(tc_ui_size size) {
        tc_widget_set_min_size(c_widget(), size);
    }
    tc_ui_size min_size() const { return tc_widget_min_size(c_widget()); }
    void set_preferred_size(tc_ui_size size) {
        tc_widget_set_preferred_size(c_widget(), size);
    }
    tc_ui_size preferred_size() const { return tc_widget_preferred_size(c_widget()); }
    void set_max_size(tc_ui_size size) {
        tc_widget_set_max_size(c_widget(), size);
    }
    tc_ui_size max_size() const { return tc_widget_max_size(c_widget()); }
    tc_ui_style computed_style(tc_ui_document* document, uint32_t extra_state_flags = 0) const;

    virtual tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints);
    virtual void layout(tc_ui_document* document, tc_ui_rect rect);
    virtual void paint(tc_ui_document* document, tc_ui_paint_context* context);
    virtual tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event);
    virtual tc_widget_handle hit_test(tc_ui_document* document, float x, float y);
    virtual tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event);
    virtual tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event);
    virtual void focus_event(tc_ui_document* document, bool focused);
    virtual void overlay_dismissed(
        tc_ui_document* document,
        tc_ui_overlay_dismiss_reason reason
    );
    virtual void on_destroy(tc_ui_document* document);

private:
    static tc_ui_size dispatch_measure(
        tc_widget* widget,
        tc_ui_document* document,
        tc_ui_constraints constraints
    );
    static void dispatch_layout(tc_widget* widget, tc_ui_document* document, tc_ui_rect rect);
    static void dispatch_paint(tc_widget* widget, tc_ui_document* document, tc_ui_paint_context* context);
    static tc_ui_event_result dispatch_pointer_event(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_pointer_event* event
    );
    static tc_widget_handle dispatch_hit_test(
        tc_widget* widget,
        tc_ui_document* document,
        float x,
        float y
    );
    static tc_ui_event_result dispatch_key_event(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_key_event* event
    );
    static tc_ui_event_result dispatch_text_event(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_text_event* event
    );
    static void dispatch_focus_event(
        tc_widget* widget,
        tc_ui_document* document,
        bool focused
    );
    static void dispatch_overlay_dismissed(
        tc_widget* widget,
        tc_ui_document* document,
        tc_ui_overlay_dismiss_reason reason
    );
    static void dispatch_on_destroy(tc_widget* widget, tc_ui_document* document);

    static const tc_widget_vtable VTABLE;
};

class DocumentBuilder {
public:
    explicit DocumentBuilder(Document& document) : document_(document) {}

    template<typename T, typename... Args>
    T& make(Args&&... args) {
        static_assert(std::is_base_of_v<Widget, T>, "T must derive from Widget");
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *widget;
        tc_widget_handle handle = document_.adopt(widget.get());
        if (tc_widget_handle_is_invalid(handle)) {
            throw std::runtime_error("failed to adopt native UI widget");
        }
        widget.release();
        return ref;
    }

    template<typename T, typename... Args>
    T& make_root(Args&&... args) {
        T& widget = make<T>(std::forward<Args>(args)...);
        if (!document_.add_root(widget)) {
            throw std::runtime_error("failed to add native UI root widget");
        }
        return widget;
    }

private:
    Document& document_;
};

class BoxLayout : public NativeWidget {
public:
    explicit BoxLayout(Orientation orientation, const char* debug_name = nullptr);

    BoxLayout& set_padding(EdgeInsets padding);
    BoxLayout& set_spacing(float spacing);
    BoxLayout& set_background(Color color);
    BoxLayout& set_border(Color color, float thickness = 1.0f);

    void add_child(tc_widget_handle handle);
    void add_child(tc_widget_handle handle, LayoutPolicy policy, float value = 0.0f);
    void add_child(const Widget& widget) { add_child(widget.handle()); }
    void add_child(const Widget& widget, LayoutPolicy policy, float value = 0.0f) {
        add_child(widget.handle(), policy, value);
    }
    void add_fixed_child(tc_widget_handle handle, float extent);
    void add_fixed_child(const Widget& widget, float extent) { add_fixed_child(widget.handle(), extent); }
    void add_preferred_child(tc_widget_handle handle);
    void add_preferred_child(const Widget& widget) { add_preferred_child(widget.handle()); }
    void add_flex_child(tc_widget_handle handle, float flex = 1.0f);
    void add_flex_child(const Widget& widget, float flex = 1.0f) { add_flex_child(widget.handle(), flex); }
    void add_stretch_child(tc_widget_handle handle);
    void add_stretch_child(const Widget& widget) { add_stretch_child(widget.handle()); }
    bool set_child_extent_limits(tc_widget_handle handle, float min_extent, float max_extent);
    bool set_child_extent_limits(const Widget& widget, float min_extent, float max_extent) {
        return set_child_extent_limits(widget.handle(), min_extent, max_extent);
    }

    const std::vector<LayoutItem>& items() const { return items_; }
    std::vector<tc_widget_handle> children() const;

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    Orientation orientation_;
    EdgeInsets padding_ {};
    float spacing_ = 0.0f;
    Color background_ {0.0f, 0.0f, 0.0f, 0.0f};
    Color border_ {0.0f, 0.0f, 0.0f, 0.0f};
    float border_thickness_ = 0.0f;
    std::vector<LayoutItem> items_;
};

class HStack : public BoxLayout {
public:
    explicit HStack(const char* debug_name = nullptr) : BoxLayout(Orientation::Horizontal, debug_name) {}
};

class VStack : public BoxLayout {
public:
    explicit VStack(const char* debug_name = nullptr) : BoxLayout(Orientation::Vertical, debug_name) {}
};

class GridLayout : public NativeWidget {
public:
    explicit GridLayout(const char* debug_name = nullptr);

    GridLayout& set_padding(EdgeInsets padding);
    GridLayout& set_spacing(float column_spacing, float row_spacing);
    GridLayout& set_background(Color color);
    GridLayout& set_border(Color color, float thickness = 1.0f);

    size_t add_column(LayoutPolicy policy = LayoutPolicy::Stretch, float value = 0.0f);
    size_t add_row(LayoutPolicy policy = LayoutPolicy::Stretch, float value = 0.0f);
    void add_child(tc_widget_handle handle, size_t row, size_t column, size_t row_span = 1, size_t column_span = 1);
    void add_child(const Widget& widget, size_t row, size_t column, size_t row_span = 1, size_t column_span = 1) {
        add_child(widget.handle(), row, column, row_span, column_span);
    }
    bool set_column_extent_limits(size_t column, float min_extent, float max_extent);
    bool set_row_extent_limits(size_t row, float min_extent, float max_extent);

    const std::vector<GridTrack>& columns() const { return columns_; }
    const std::vector<GridTrack>& rows() const { return rows_; }
    const std::vector<GridItem>& items() const { return items_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    EdgeInsets padding_ {};
    float column_spacing_ = 0.0f;
    float row_spacing_ = 0.0f;
    Color background_ {0.0f, 0.0f, 0.0f, 0.0f};
    Color border_ {0.0f, 0.0f, 0.0f, 0.0f};
    float border_thickness_ = 0.0f;
    std::vector<GridTrack> columns_;
    std::vector<GridTrack> rows_;
    std::vector<GridItem> items_;
};

class GroupBox : public NativeWidget {
public:
    explicit GroupBox(std::string title = {}, const char* debug_name = nullptr);

    GroupBox& set_title(std::string title);
    GroupBox& set_padding(EdgeInsets padding);
    GroupBox& set_background(Color color);
    GroupBox& set_border(Color color, float thickness = 1.0f);
    void set_content(tc_widget_handle handle);
    void set_content(const Widget& widget) { set_content(widget.handle()); }

    const std::string& title() const { return title_; }
    tc_widget_handle content() const { return child_handle_at(0); }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    tc_ui_rect content_rect(tc_ui_document* document) const;

    std::string title_;
    float header_height_ = 30.0f;
};

class Splitter : public NativeWidget {
public:
    explicit Splitter(Orientation orientation = Orientation::Horizontal, const char* debug_name = nullptr);

    void set_first(tc_widget_handle handle);
    void set_first(const Widget& widget) { set_first(widget.handle()); }
    void set_second(tc_widget_handle handle);
    void set_second(const Widget& widget) { set_second(widget.handle()); }
    Splitter& set_split_fraction(float fraction);
    Splitter& set_min_extents(float first_min, float second_min);
    Splitter& set_divider_thickness(float thickness);

    tc_widget_handle first() const { return child_handle_at(0); }
    tc_widget_handle second() const { return child_handle_at(1); }
    float split_fraction() const { return split_fraction_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    tc_ui_rect divider_rect() const;
    void layout_children(tc_ui_document* document);
    float split_axis_extent() const;

    Orientation orientation_ = Orientation::Horizontal;
    float split_fraction_ = 0.5f;
    float first_min_extent_ = 32.0f;
    float second_min_extent_ = 32.0f;
    float divider_thickness_ = 6.0f;
    Color divider_color_ {0.30f, 0.33f, 0.38f, 1.0f};
};

class ScrollArea : public NativeWidget {
public:
    explicit ScrollArea(const char* debug_name = nullptr);

    void set_content(tc_widget_handle handle);
    void set_content(const Widget& widget) { set_content(widget.handle()); }
    tc_widget_handle content() const { return child_handle_at(0); }

    void set_scroll(float x, float y);
    float scroll_x() const { return scroll_x_; }
    float scroll_y() const { return scroll_y_; }
    tc_ui_size content_size() const { return content_size_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    void clamp_scroll();

    tc_ui_size content_size_ {0.0f, 0.0f};
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    float wheel_step_ = 48.0f;
};

struct TabPage {
    std::string title;
    tc_widget_handle handle = tc_widget_handle_invalid();
};

class TabView : public NativeWidget {
public:
    explicit TabView(const char* debug_name = nullptr);

    void add_page(std::string title, tc_widget_handle handle);
    void add_page(std::string title, const Widget& widget) { add_page(std::move(title), widget.handle()); }
    size_t page_count() const { return child_count(); }
    size_t selected_index() const { return selected_index_; }
    void set_selected_index(size_t index);

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
};

class Panel : public NativeWidget {
public:
    explicit Panel(const char* debug_name = nullptr);

    Panel& set_fill(Color color);
    Panel& set_border(Color color, float thickness = 1.0f);

    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
};

class Button : public NativeWidget {
public:
    explicit Button(std::string text = {});
    Button(std::string text, Color fill);
    explicit Button(Color fill);

    Button& set_accent(Color color);
    Button& set_text(std::string text);
    Signal<Button&>& clicked() { return clicked_; }
    const Signal<Button&>& clicked() const { return clicked_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    std::string text_;
    bool pressed_ = false;
    Signal<Button&> clicked_;
};

class Label : public NativeWidget {
public:
    explicit Label(std::string text);
    Label(std::string text, float font_size);
    Label(std::string text, float font_size, Color color);

    Label& set_text(std::string text);
    Label& set_color(Color color);
    Label& set_font_size(float font_size);
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    void update_unmeasured_size();

    std::string text_;
};

class Checkbox : public NativeWidget {
public:
    explicit Checkbox(bool checked = false);

    bool checked() const { return checked_; }
    void set_checked(bool checked);
    Signal<Checkbox&, bool>& changed() { return changed_; }
    const Signal<Checkbox&, bool>& changed() const { return changed_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    bool checked_ = false;
    bool pressed_ = false;
    Signal<Checkbox&, bool> changed_;
};

class ProgressBar : public NativeWidget {
public:
    explicit ProgressBar(float value = 0.0f);

    void set_value(float value);
    float value() const { return value_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    float value_ = 0.0f;
};

class Separator : public NativeWidget {
public:
    explicit Separator(Orientation orientation = Orientation::Horizontal);

    Separator& set_color(Color color);
    Separator& set_thickness(float thickness);
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    Orientation orientation_ = Orientation::Horizontal;
};

class TextInput : public NativeWidget {
public:
    explicit TextInput(std::string text = {});

    const std::string& text() const { return text_; }
    size_t caret() const { return caret_; }
    bool has_selection() const;
    size_t selection_start() const;
    size_t selection_end() const;
    std::string selected_text() const;
    float scroll_x() const { return scroll_x_; }
    void set_text(std::string text);
    void set_caret(size_t caret);
    void select(size_t anchor, size_t caret);
    void select_all();
    void clear_selection();
    Signal<TextInput&, const std::string&>& changed() { return changed_; }
    const Signal<TextInput&, const std::string&>& changed() const { return changed_; }
    Signal<TextInput&, const std::string&>& submitted() { return submitted_; }
    const Signal<TextInput&, const std::string&>& submitted() const { return submitted_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;

private:
    tc_ui_rect text_clip_rect(tc_ui_document* document) const;
    bool measure_prefix(
        tc_ui_document* document,
        size_t byte_offset,
        float font_size,
        float& width
    ) const;
    void ensure_caret_visible(tc_ui_document* document);
    size_t caret_from_content_x(tc_ui_document* document, float content_x) const;
    void update_unmeasured_size();
    void emit_changed();
    void move_caret(size_t next, bool extend_selection);
    bool delete_selection();
    bool replace_selection(std::string_view inserted);

    std::string text_;
    size_t caret_ = 0;
    size_t selection_anchor_ = SIZE_MAX;
    bool selecting_ = false;
    float scroll_x_ = 0.0f;
    Signal<TextInput&, const std::string&> changed_;
    Signal<TextInput&, const std::string&> submitted_;
};

class TextArea : public NativeWidget {
public:
    explicit TextArea(std::string text = {});

    const std::string& text() const { return text_; }
    size_t caret() const { return caret_; }
    bool has_selection() const;
    size_t selection_start() const;
    size_t selection_end() const;
    std::string selected_text() const;
    float scroll_x() const { return scroll_x_; }
    float scroll_y() const { return scroll_y_; }
    void set_text(std::string text);
    void set_caret(size_t caret);
    void select(size_t anchor, size_t caret);
    void select_all();
    void clear_selection();
    Signal<TextArea&, const std::string&>& changed() { return changed_; }
    const Signal<TextArea&, const std::string&>& changed() const { return changed_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;

private:
    struct Line {
        size_t start;
        size_t end;
    };

    std::vector<Line> lines() const;
    tc_ui_rect text_clip_rect(tc_ui_document* document) const;
    float line_height(tc_ui_document* document) const;
    bool measure_range(
        tc_ui_document* document,
        size_t start,
        size_t end,
        float font_size,
        float& width
    ) const;
    size_t line_index_for_offset(const std::vector<Line>& lines, size_t offset) const;
    size_t caret_from_point(tc_ui_document* document, float x, float y) const;
    size_t caret_from_line_x(
        tc_ui_document* document,
        const Line& line,
        float content_x
    ) const;
    void ensure_caret_visible(tc_ui_document* document);
    void move_caret(size_t next, bool extend_selection, bool preserve_column = false);
    void move_vertical(tc_ui_document* document, int direction, bool extend_selection);
    bool delete_selection();
    bool replace_selection(std::string_view inserted);
    void emit_changed();

    std::string text_;
    size_t caret_ = 0;
    size_t selection_anchor_ = SIZE_MAX;
    bool selecting_ = false;
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    float desired_x_ = -1.0f;
    Signal<TextArea&, const std::string&> changed_;
};

class Slider : public NativeWidget {
public:
    explicit Slider(float value = 0.0f);

    void set_value(float value);
    float value() const { return value_; }
    float min_value() const { return min_value_; }
    float max_value() const { return max_value_; }
    float step() const { return step_; }
    void set_range(float min_value, float max_value);
    void set_step(float step);
    Signal<Slider&, float>& changed() { return changed_; }
    const Signal<Slider&, float>& changed() const { return changed_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    float value_ = 0.0f;
    float min_value_ = 0.0f;
    float max_value_ = 1.0f;
    float step_ = 0.0f;
    bool dragging_ = false;
    Signal<Slider&, float> changed_;
};

class SpinBox : public NativeWidget {
public:
    explicit SpinBox(float value = 0.0f);

    float value() const { return value_; }
    float min_value() const { return min_value_; }
    float max_value() const { return max_value_; }
    float step() const { return step_; }
    int decimals() const { return decimals_; }
    bool editing() const { return editing_; }
    const std::string& edit_text() const { return edit_text_; }
    void set_value(float value);
    void set_range(float min_value, float max_value);
    void set_step(float step);
    void set_decimals(int decimals);
    Signal<SpinBox&, float>& changed() { return changed_; }
    const Signal<SpinBox&, float>& changed() const { return changed_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;
    void focus_event(tc_ui_document* document, bool focused) override;

private:
    std::string formatted_value() const;
    void begin_edit();
    void commit_edit();
    void cancel_edit();
    tc_ui_rect up_button_rect() const;
    tc_ui_rect down_button_rect() const;

    float value_ = 0.0f;
    float min_value_ = -1000000000.0f;
    float max_value_ = 1000000000.0f;
    float step_ = 1.0f;
    int decimals_ = 2;
    bool editing_ = false;
    std::string edit_text_;
    size_t caret_ = 0;
    float button_width_ = 18.0f;
    Signal<SpinBox&, float> changed_;
};

class SliderEdit : public NativeWidget {
public:
    explicit SliderEdit(float value = 0.0f);

    float value() const { return value_; }
    void set_value(float value);
    void set_range(float min_value, float max_value);
    void set_step(float step);
    void set_decimals(int decimals);
    void set_label(std::string label);
    const std::string& label() const { return label_; }
    tc_widget_handle slider_handle() const { return slider_handle_; }
    tc_widget_handle spin_box_handle() const { return spin_box_handle_; }
    Signal<SliderEdit&, float>& changed() { return changed_; }
    const Signal<SliderEdit&, float>& changed() const { return changed_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    void on_destroy(tc_ui_document* document) override;

private:
    bool ensure_children(tc_ui_document* document);
    void sync_children(tc_ui_document* document);

    float value_ = 0.0f;
    float min_value_ = 0.0f;
    float max_value_ = 1.0f;
    float step_ = 0.0f;
    int decimals_ = 2;
    float spacing_ = 4.0f;
    float spin_box_width_ = 80.0f;
    std::string label_;
    tc_widget_handle slider_handle_ = tc_widget_handle_invalid();
    tc_widget_handle spin_box_handle_ = tc_widget_handle_invalid();
    size_t slider_connection_ = 0;
    size_t spin_box_connection_ = 0;
    bool syncing_ = false;
    Signal<SliderEdit&, float> changed_;
};

class ComboBoxPopup;

class ComboBox : public NativeWidget {
public:
    ComboBox();

    size_t item_count() const { return items_.size(); }
    const std::string& item_text(size_t index) const;
    int selected_index() const { return selected_index_; }
    std::string selected_text() const;
    bool open() const { return open_; }
    void add_item(std::string item);
    void clear_items();
    void set_selected_index(int index);
    Signal<ComboBox&, int, const std::string&>& changed() { return changed_; }
    const Signal<ComboBox&, int, const std::string&>& changed() const { return changed_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    void on_destroy(tc_ui_document* document) override;

private:
    friend class ComboBoxPopup;
    bool show_popup(tc_ui_document* document);
    void hide_popup(tc_ui_document* document);
    void popup_dismissed();

    std::vector<std::string> items_;
    int selected_index_ = -1;
    bool open_ = false;
    tc_widget_handle popup_handle_ = tc_widget_handle_invalid();
    float item_height_ = 24.0f;
    size_t max_visible_items_ = 8;
    Signal<ComboBox&, int, const std::string&> changed_;
};

class IconButton : public NativeWidget {
public:
    explicit IconButton(std::string icon = {});

    void set_icon(std::string icon);
    void set_texture(uint32_t texture_id);
    void set_active(bool active);
    bool active() const { return active_; }
    Signal<IconButton&>& clicked() { return clicked_; }
    const Signal<IconButton&>& clicked() const { return clicked_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    std::string icon_;
    uint32_t texture_id_ = 0;
    bool active_ = false;
    bool pressed_ = false;
    Signal<IconButton&> clicked_;
};

class ImageWidget : public NativeWidget {
public:
    ImageWidget();

    void set_texture(uint32_t texture_id, tc_ui_size intrinsic_size = {});
    uint32_t texture_id() const { return texture_id_; }
    tc_ui_size intrinsic_size() const { return intrinsic_size_; }
    void set_tint(Color tint);
    void set_preserve_aspect(bool preserve) { preserve_aspect_ = preserve; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    uint32_t texture_id_ = 0;
    tc_ui_size intrinsic_size_ {64.0f, 64.0f};
    Color tint_ {1.0f, 1.0f, 1.0f, 1.0f};
    bool preserve_aspect_ = true;
};

class Canvas : public NativeWidget {
public:
    using PaintCallback = std::function<void(Canvas&, tc_ui_paint_context*)>;

    Canvas();

    void set_texture(uint32_t texture_id, tc_ui_size image_size = {});
    void set_overlay_texture(uint32_t texture_id);
    void set_paint_callback(PaintCallback callback);
    float zoom() const { return zoom_; }
    void set_zoom(float zoom, tc_ui_point anchor);
    void fit_in_view();
    tc_ui_point widget_to_image(tc_ui_point point) const;
    tc_ui_point image_to_widget(tc_ui_point point) const;
    Signal<Canvas&, float>& zoom_changed() { return zoom_changed_; }
    Signal<Canvas&, tc_ui_point, const tc_ui_pointer_event&>& pointer_input() { return pointer_input_; }

    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    uint32_t texture_id_ = 0;
    uint32_t overlay_texture_id_ = 0;
    tc_ui_size image_size_ {};
    float zoom_ = 1.0f;
    float min_zoom_ = 0.01f;
    float max_zoom_ = 100.0f;
    tc_ui_point offset_ {};
    bool panning_ = false;
    tc_ui_point pan_start_ {};
    tc_ui_point pan_start_offset_ {};
    PaintCallback paint_callback_;
    Signal<Canvas&, float> zoom_changed_;
    Signal<Canvas&, tc_ui_point, const tc_ui_pointer_event&> pointer_input_;
};

class Swatch : public NativeWidget {
public:
    explicit Swatch(Color color);
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    Color color_;
};

class Spacer : public NativeWidget {
public:
    explicit Spacer(tc_ui_size size);
};

} // namespace termin::gui_native
