#include <termin/gui_native/tc_document.hpp>

#include "widgets_test_support.hpp"

namespace termin_gui_native_test {
    void test_controls_handle_pointer_events() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);
        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& checkbox = ui.make<Checkbox>(false);
        auto& slider = ui.make<Slider>(0.0f);
        root.add_child(checkbox);
        root.add_child(slider);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 40.0f});

        tc_ui_pointer_event checkbox_event{};
        checkbox_event.type = TC_UI_POINTER_DOWN;
        checkbox_event.x = checkbox.bounds().x + 4.0f;
        checkbox_event.y = checkbox.bounds().y + 4.0f;
        assert(document.dispatch_pointer_event(checkbox_event) == TC_UI_EVENT_HANDLED);
        assert(!checkbox.checked());
        assert(tc_widget_handle_eq(document.pointer_capture(), checkbox.handle()));
        checkbox_event.type = TC_UI_POINTER_UP;
        assert(document.dispatch_pointer_event(checkbox_event) == TC_UI_EVENT_HANDLED);
        assert(checkbox.checked());
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));

        tc_ui_pointer_event slider_event{};
        slider_event.type = TC_UI_POINTER_DOWN;
        slider_event.x = slider.bounds().x + 10.0f;
        slider_event.y = slider.bounds().y + slider.bounds().height * 0.5f;
        assert(document.dispatch_pointer_event(slider_event) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_eq(document.pointer_capture(), slider.handle()));
        slider_event.type = TC_UI_POINTER_MOVE;
        slider_event.x = slider.bounds().x + slider.bounds().width + 80.0f;
        assert(document.dispatch_pointer_event(slider_event) == TC_UI_EVENT_HANDLED);
        assert(slider.value() > 0.95f);
        slider_event.type = TC_UI_POINTER_UP;
        assert(document.dispatch_pointer_event(slider_event) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));

        tc_ui_document_destroy(document_handle);
    }

    void test_separator_layout_and_paint_command() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);
        auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
        root.set_padding(EdgeInsets{});
        auto& separator = ui.make<Separator>(Orientation::Horizontal);
        separator.set_thickness(3.0f);
        root.add_preferred_child(separator);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 20.0f});
        assert(near(separator.bounds().width, 120.0f));
        assert(near(separator.bounds().height, 3.0f));

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(paint_context);
        assert(tc_ui_draw_list_command_count(draw_list) >= 3);
        bool saw_fill = false;
        for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, i);
            if (command && command->type == TC_UI_DRAW_FILL_RECT && near(command->rect.height, 3.0f)) {
                saw_fill = true;
            }
        }
        assert(saw_fill);
        tc_ui_paint_context_destroy(paint_context);
        tc_ui_draw_list_destroy(draw_list);

        tc_ui_document_destroy(document_handle);
    }

    void test_text_input_focus_text_edit_and_submit() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& input = ui.make<TextInput>("ab");
        root.add_preferred_child(input);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 40.0f});

        int changed = 0;
        int submitted = 0;
        std::string last_text;
        input.changed().connect([&changed, &last_text](TextInput&, const std::string& text) {
            changed += 1;
            last_text = text;
        });
        input.submitted().connect([&submitted](TextInput&, const std::string&) { submitted += 1; });

        tc_ui_pointer_event pointer{};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = input.bounds().x + 40.0f;
        pointer.y = input.bounds().y + 10.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_eq(document.focused_widget(), input.handle()));

        tc_ui_text_event text{};
        text.text = "c";
        assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "abc");
        assert(input.caret() == 3);
        assert(changed == 1);
        assert(last_text == "abc");

        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.key = TC_UI_KEY_LEFT;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.caret() == 2);

        key.key = TC_UI_KEY_BACKSPACE;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "ac");
        assert(input.caret() == 1);
        assert(changed == 2);

        key.key = TC_UI_KEY_DELETE;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "a");
        assert(input.caret() == 1);
        assert(changed == 3);

        text.text = "\r\nb\nc";
        assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "a b c");
        assert(input.text().find('\r') == std::string::npos);
        assert(input.text().find('\n') == std::string::npos);
        assert(changed == 4);

        key.key = TC_UI_KEY_ENTER;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(submitted == 1);

        input.set_text({});
        assert(changed == 5);
        input.set_placeholder("First placeholder");
        assert(input.placeholder() == "First placeholder");
        input.set_placeholder("Replacement placeholder");
        assert(input.placeholder() == "Replacement placeholder");

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        bool saw_placeholder = false;
        for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
            if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
                std::strcmp(command->text, "Replacement placeholder") == 0) {
                saw_placeholder = true;
                break;
            }
        }
        assert(saw_placeholder);
        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);

        input.set_placeholder({});
        assert(input.placeholder().empty());

        tc_ui_document_destroy(document_handle);
    }

    void test_text_widgets_clip_text_paint() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
        auto& label = ui.make<Label>("Long label text that must stay inside its widget", 14.0f);
        auto& input = ui.make<TextInput>("Long input text that must stay inside the edit box");
        root.add_fixed_child(label, 20.0f);
        root.add_fixed_child(input, 34.0f);

        document.layout_roots(tc_ui_rect{10.0f, 20.0f, 180.0f, 80.0f});

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(paint_context);

        bool saw_label_clip = false;
        bool saw_input_inner_clip = false;
        for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, i);
            if (!command || command->type != TC_UI_DRAW_PUSH_CLIP) {
                continue;
            }
            if (near(command->rect.x, label.bounds().x) && near(command->rect.y, label.bounds().y) &&
                near(command->rect.width, label.bounds().width) && near(command->rect.height, label.bounds().height)) {
                saw_label_clip = true;
            }
            if (near(command->rect.x, input.bounds().x + 8.0f) && near(command->rect.y, input.bounds().y + 2.0f) &&
                near(command->rect.width, input.bounds().width - 16.0f) &&
                near(command->rect.height, input.bounds().height - 4.0f)) {
                saw_input_inner_clip = true;
            }
        }

        assert(saw_label_clip);
        assert(saw_input_inner_clip);
        assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) == count_commands(draw_list, TC_UI_DRAW_POP_CLIP));

        tc_ui_paint_context_destroy(paint_context);
        tc_ui_draw_list_destroy(draw_list);

        tc_ui_document_destroy(document_handle);
    }

    void test_text_measurement_uses_proportional_metrics() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        auto& narrow = ui.make<Label>("iii", 20.0f);
        auto& wide = ui.make<Label>("WWW", 20.0f);
        const tc_ui_constraints constraints{tc_ui_size{0.0f, 0.0f}, tc_ui_size{1000.0f, 1000.0f}};

        const tc_ui_size narrow_size = narrow.measure(document.get(), constraints);
        const tc_ui_size wide_size = wide.measure(document.get(), constraints);
        assert(near(narrow_size.width, 15.0f));
        assert(near(wide_size.width, 54.0f));
        assert(wide_size.width > narrow_size.width * 3.0f);
        assert(near(narrow_size.height, 24.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_wrapped_label_and_wrap_layout_reflow() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);

        auto& label = ui.make<Label>("one two\nтри", 10.0f);
        label.set_wrap_mode(TextWrapMode::Word);
        const tc_ui_constraints narrow_constraints{tc_ui_size{0.0f, 0.0f}, tc_ui_size{25.0f, 200.0f}};
        const tc_ui_constraints wide_constraints{tc_ui_size{0.0f, 0.0f}, tc_ui_size{100.0f, 200.0f}};
        const tc_ui_size narrow = label.measure(document.get(), narrow_constraints);
        const tc_ui_size wide = label.measure(document.get(), wide_constraints);
        assert(near(narrow.width, 18.0f));
        assert(near(narrow.height, 36.0f));
        assert(near(wide.width, 35.0f));
        assert(near(wide.height, 24.0f));

        auto metrics = tc_ui_presentation_metrics_identity(tc_ui_size{200.0f, 200.0f});
        metrics.font_scale = 2.0f;
        assert(document.set_presentation_metrics(metrics));
        const tc_ui_size accessible = label.measure(document.get(), wide_constraints);
        assert(accessible.height > wide.height);
        assert(accessible.width <= wide_constraints.max_size.width);

        auto& ellipsis = ui.make<Label>("неразрывныйтокен", 10.0f);
        ellipsis.set_wrap_mode(TextWrapMode::Word).set_overflow(TextOverflow::Ellipsis).set_max_lines(1);
        assert(document.add_root(ellipsis));
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 40.0f, 30.0f});
        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        bool saw_ellipsis = false;
        for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
            if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
                std::string(command->text).find("\xE2\x80\xA6") != std::string::npos) {
                saw_ellipsis = true;
            }
        }
        assert(saw_ellipsis);
        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);

        metrics.font_scale = 1.0f;
        assert(document.set_presentation_metrics(metrics));
        auto& flow = ui.make_root<WrapLayout>(Orientation::Horizontal, "flow");
        flow.set_spacing(5.0f).set_line_spacing(4.0f);
        auto& first = ui.make<IconButton>("1");
        auto& second = ui.make<IconButton>("2");
        auto& third = ui.make<IconButton>("3");
        first.set_preferred_size({30.0f, 20.0f});
        second.set_preferred_size({30.0f, 20.0f});
        third.set_preferred_size({30.0f, 20.0f});
        flow.add_child(first);
        flow.add_child(second);
        flow.add_child(third);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 70.0f, 100.0f});
        assert(near(first.bounds().y, second.bounds().y));
        assert(third.bounds().y > first.bounds().y);
        assert(document.set_focus(second));
        const tc_widget_handle focused = document.focused_widget();
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 34.0f, 100.0f});
        assert(second.bounds().y > first.bounds().y);
        assert(third.bounds().y > second.bounds().y);
        assert(tc_widget_handle_eq(document.focused_widget(), focused));

        flow.set_orientation(Orientation::Vertical);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 45.0f});
        assert(near(first.bounds().x, second.bounds().x));
        assert(third.bounds().x > first.bounds().x);
        assert(tc_widget_handle_eq(document.focused_widget(), focused));

        auto& scroll = ui.make_root<ScrollArea>();
        scroll.set_scroll_axes(false, true);
        auto& scroll_label = ui.make<Label>("one two three four five six", 10.0f);
        scroll_label.set_wrap_mode(TextWrapMode::Word);
        scroll.set_content(scroll_label);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 45.0f, 20.0f});
        const float narrow_content_height = scroll.content_size().height;
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 20.0f});
        assert(scroll.content_size().height < narrow_content_height);

        tc_ui_document_destroy(document_handle);
    }

    void test_text_input_edits_utf8_at_codepoint_boundaries() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        const std::string initial = "a\xc3\xa9\xf0\x9f\x99\x82"
                                    "b";
        auto& input = ui.make_root<TextInput>(initial);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 40.0f});
        assert(input.caret() == 8);

        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.key = TC_UI_KEY_LEFT;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_IGNORED);
        assert(document.set_focus(input));
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.caret() == 7);

        key.key = TC_UI_KEY_BACKSPACE;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "a\xc3\xa9"
                               "b");
        assert(input.caret() == 3);

        key.key = TC_UI_KEY_DELETE;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "a\xc3\xa9");
        assert(input.caret() == 3);

        input.set_caret(2);
        assert(input.caret() == 1);
        tc_ui_text_event insert{};
        insert.text = "\xf0\x9f\x99\x82";
        assert(document.dispatch_text_event(insert) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "a\xf0\x9f\x99\x82\xc3\xa9");
        assert(input.caret() == 5);

        tc_ui_text_event invalid{};
        invalid.text = "\xc3(";
        assert(document.dispatch_text_event(invalid) == TC_UI_EVENT_IGNORED);
        assert(input.text() == "a\xf0\x9f\x99\x82\xc3\xa9");

        tc_ui_document_destroy(document_handle);
    }

    void test_text_input_scrolls_to_keep_caret_inside_clip() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        auto& input = ui.make_root<TextInput>("WWWWWWWWWWWW");
        assert(document.set_focus(input));
        document.layout_roots(tc_ui_rect{20.0f, 30.0f, 80.0f, 34.0f});
        assert(input.scroll_x() > 0.0f);

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(paint_context);

        const float clip_left = input.bounds().x + 8.0f;
        const float clip_right = input.bounds().x + input.bounds().width - 8.0f;
        bool saw_shifted_text = false;
        bool saw_visible_caret = false;
        for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
            if (!command) {
                continue;
            }
            if (command->type == TC_UI_DRAW_TEXT && command->p0.x < clip_left) {
                saw_shifted_text = true;
            }
            if (command->type == TC_UI_DRAW_LINE && near(command->p0.x, command->p1.x) && command->p0.x >= clip_left &&
                command->p0.x <= clip_right) {
                saw_visible_caret = true;
            }
        }
        assert(saw_shifted_text);
        assert(saw_visible_caret);

        tc_ui_paint_context_destroy(paint_context);
        tc_ui_draw_list_destroy(draw_list);

        tc_ui_document_destroy(document_handle);
    }

    void test_text_input_utf8_selection_and_host_clipboard() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        TestClipboard clipboard;
        install_test_clipboard(document, clipboard);
        DocumentBuilder ui(document);
        auto& input = ui.make_root<TextInput>("a\xc3\xa9\xf0\x9f\x99\x82"
                                              "b");
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 34.0f});
        assert(document.set_focus(input));

        input.select(1, 7);
        assert(input.has_selection());
        assert(input.selected_text() == "\xc3\xa9\xf0\x9f\x99\x82");

        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.modifiers = TC_UI_MOD_CTRL;
        key.key = 'c';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(clipboard.text == "\xc3\xa9\xf0\x9f\x99\x82");

        key.key = 'x';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "ab");
        assert(input.caret() == 1);
        assert(!input.has_selection());

        key.key = 'v';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.text() == "a\xc3\xa9\xf0\x9f\x99\x82"
                               "b");
        assert(input.caret() == 7);

        key.modifiers = TC_UI_MOD_SHIFT;
        key.key = TC_UI_KEY_LEFT;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.selected_text() == "\xf0\x9f\x99\x82");

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        assert(count_commands(draw_list, TC_UI_DRAW_FILL_ROUNDED_RECT) >= 1);
        assert(count_commands(draw_list, TC_UI_DRAW_FILL_RECT) >= 1);
        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);

        input.set_caret(input.text().size());
        clipboard.text = "\r\nnext\nline";
        key.modifiers = TC_UI_MOD_CTRL;
        key.key = 'v';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(input.text().ends_with("b next line"));
        assert(input.text().find('\r') == std::string::npos);
        assert(input.text().find('\n') == std::string::npos);

        tc_ui_document_destroy(document_handle);
    }

    void test_text_area_multiline_utf8_editing_navigation_and_scroll() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        TestClipboard clipboard;
        install_test_clipboard(document, clipboard);
        DocumentBuilder ui(document);
        auto& area = ui.make_root<TextArea>("a\xc3\xa9\nWWWWWWWW\n\xf0\x9f\x99\x82z\nlast");
        document.layout_roots(tc_ui_rect{10.0f, 20.0f, 70.0f, 42.0f});
        assert(document.set_focus(area));
        int changed = 0;
        area.changed().connect([&changed](TextArea&, const std::string&) { ++changed; });
        assert(area.scroll_y() > 0.0f);
        area.set_caret(12);
        document.layout_roots(tc_ui_rect{10.0f, 20.0f, 70.0f, 42.0f});
        assert(area.scroll_x() > 0.0f);

        area.select(1, 13);
        assert(area.selected_text() == "\xc3\xa9\nWWWWWWWW\n");
        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.modifiers = TC_UI_MOD_CTRL;
        key.key = 'c';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(clipboard.text == "\xc3\xa9\nWWWWWWWW\n");

        key.key = 'x';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(area.text() == "a\xf0\x9f\x99\x82z\nlast");
        assert(area.caret() == 1);

        key.key = 'v';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(area.text() == "a\xc3\xa9\nWWWWWWWW\n\xf0\x9f\x99\x82z\nlast");
        assert(area.caret() == 13);

        area.set_caret(area.text().size());
        key.modifiers = 0;
        key.key = TC_UI_KEY_UP_ARROW;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(area.caret() == 18);
        key.modifiers = TC_UI_MOD_SHIFT;
        key.key = TC_UI_KEY_HOME;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(area.selected_text() == "\xf0\x9f\x99\x82z");

        tc_ui_text_event text{"Q"};
        assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
        assert(area.text() == "a\xc3\xa9\nWWWWWWWW\nQ\nlast");
        assert(changed == 3);

        area.set_text({});
        assert(changed == 4);
        area.set_placeholder("First placeholder");
        assert(area.placeholder() == "First placeholder");
        area.set_placeholder("Replacement placeholder");
        assert(area.placeholder() == "Replacement placeholder");

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        bool saw_placeholder = false;
        for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
            if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
                std::strcmp(command->text, "Replacement placeholder") == 0) {
                saw_placeholder = true;
                break;
            }
        }
        assert(saw_placeholder);
        assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) == 1);
        assert(count_commands(draw_list, TC_UI_DRAW_POP_CLIP) == 1);
        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);

        area.set_placeholder({});
        assert(area.placeholder().empty());

        tc_ui_document_destroy(document_handle);
    }

    void test_spin_box_numeric_edit_buttons_and_keys() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        TestClipboard clipboard;
        install_test_clipboard(document, clipboard);
        DocumentBuilder ui(document);
        auto& spin = ui.make_root<SpinBox>(5.0f);
        spin.set_range(-10.0f, 10.0f);
        spin.set_step(0.5f);
        spin.set_decimals(1);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 34.0f});
        assert(document.set_focus(spin));
        int changes = 0;
        spin.changed().connect([&changes](SpinBox&, float) { ++changes; });

        tc_ui_pointer_event pointer{};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = spin.bounds().x + spin.bounds().width - 4.0f;
        pointer.y = spin.bounds().y + 4.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(near(spin.value(), 5.5f));

        pointer.x = spin.bounds().x + 18.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(spin.editing());
        assert(spin.caret() < spin.edit_text().size());

        pointer.type = TC_UI_POINTER_MOVE;
        pointer.x = spin.bounds().x + 34.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(spin.has_selection());
        assert(!spin.selected_text().empty());
        pointer.type = TC_UI_POINTER_UP;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);

        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.modifiers = TC_UI_MOD_CTRL;
        key.key = 'a';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        key.key = 'c';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(clipboard.text == spin.edit_text());
        key.key = 'x';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(spin.edit_text().empty());
        clipboard.text = "6.5";
        key.key = 'v';
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(spin.edit_text() == "6.5");

        key.modifiers = 0;
        key.key = TC_UI_KEY_HOME;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        const size_t initial_length = spin.edit_text().size();
        key.key = TC_UI_KEY_DELETE;
        for (size_t index = 0; index < initial_length; ++index) {
            assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        }
        tc_ui_text_event text{"7.5"};
        assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
        key.key = TC_UI_KEY_ENTER;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(!spin.editing());
        assert(near(spin.value(), 7.5f));
        assert(changes == 2);

        tc_ui_document_destroy(document_handle);
    }

    void test_slider_edit_owns_canonical_children_and_syncs_values() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        auto& edit = ui.make_root<SliderEdit>(2.0f);
        assert(tc_widget_handle_is_invalid(edit.slider_handle()));
        assert(tc_widget_handle_is_invalid(edit.spin_box_handle()));
        edit.set_range(0.0f, 10.0f);
        edit.set_step(0.5f);
        edit.set_label("Exposure");
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 300.0f, 52.0f});
        assert(edit.child_count() == 2);
        assert(tc_ui_document_is_alive(document.get(), edit.slider_handle()));
        assert(tc_ui_document_is_alive(document.get(), edit.spin_box_handle()));
        tc_widget* slider_widget = tc_ui_document_resolve_widget(document.get(), edit.slider_handle());
        tc_widget* spin_widget = tc_ui_document_resolve_widget(document.get(), edit.spin_box_handle());
        assert(tc_widget_bounds(slider_widget).width > 0.0f);
        assert(tc_widget_bounds(spin_widget).width > 0.0f);

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(paint_context);
        assert(count_commands(draw_list, TC_UI_DRAW_LINE) >= 2);
        assert(count_commands(draw_list, TC_UI_DRAW_FILL_RECT) >= 3);
        assert(count_commands(draw_list, TC_UI_DRAW_TEXT) >= 4);
        tc_ui_paint_context_destroy(paint_context);
        tc_ui_draw_list_destroy(draw_list);

        const tc_ui_rect slider_bounds = tc_widget_bounds(slider_widget);
        assert(tc_widget_handle_eq(document.hit_test(slider_bounds.x + slider_bounds.width * 0.5f,
                                                     slider_bounds.y + slider_bounds.height * 0.5f),
                                   edit.slider_handle()));
        const tc_ui_rect spin_bounds = tc_widget_bounds(spin_widget);
        assert(tc_widget_handle_eq(
            document.hit_test(spin_bounds.x + spin_bounds.width * 0.5f, spin_bounds.y + spin_bounds.height * 0.5f),
            edit.spin_box_handle()));

        int changes = 0;
        edit.changed().connect([&changes](SliderEdit&, float) { ++changes; });
        tc_ui_pointer_event pointer{};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = slider_bounds.x + slider_bounds.width - 1.0f;
        pointer.y = slider_bounds.y + slider_bounds.height * 0.5f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        pointer.type = TC_UI_POINTER_UP;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(near(edit.value(), 10.0f));
        assert(changes == 1);
        assert(near(static_cast<SpinBox*>(spin_widget->body)->value(), 10.0f));

        assert(tc_ui_document_set_focus(document.get(), edit.spin_box_handle()));
        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.key = TC_UI_KEY_DOWN_ARROW;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(near(edit.value(), 9.5f));
        assert(changes == 2);

        auto* slider = static_cast<Slider*>(slider_widget->body);
        slider->set_value(7.0f);
        assert(near(edit.value(), 7.0f));
        assert(near(static_cast<SpinBox*>(spin_widget->body)->value(), 7.0f));
        assert(changes == 3);

        const tc_widget_handle slider_handle = edit.slider_handle();
        const tc_widget_handle spin_box_handle = edit.spin_box_handle();
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 360.0f, 52.0f});
        assert(edit.child_count() == 2);
        assert(tc_widget_handle_eq(edit.slider_handle(), slider_handle));
        assert(tc_widget_handle_eq(edit.spin_box_handle(), spin_box_handle));

        const tc_widget_handle root_handle = edit.handle();
        assert(tc_ui_document_destroy_widget_recursive(document.get(), root_handle));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);

        tc_ui_document_handle destructive_document_handle = tc_ui_document_create();
        TcDocument destructive_document(destructive_document_handle);
        install_test_text_measurer(destructive_document);
        DocumentBuilder destructive_ui(destructive_document);
        auto& destroying_edit = destructive_ui.make_root<SliderEdit>(0.25f);
        destroying_edit.set_range(0.0f, 1.0f);
        destructive_document.layout_roots(tc_ui_rect{0.0f, 0.0f, 300.0f, 34.0f});

        const tc_widget_handle destroying_edit_handle = destroying_edit.handle();
        tc_widget* destroying_slider_widget =
            tc_ui_document_resolve_widget(destructive_document.get(), destroying_edit.slider_handle());
        assert(destroying_slider_widget);
        auto* destroying_slider = static_cast<Slider*>(destroying_slider_widget->body);
        destroying_edit.changed().connect([&destructive_document, destroying_edit_handle](SliderEdit&, float) {
            assert(tc_ui_document_destroy_widget_recursive(destructive_document.get(), destroying_edit_handle));
        });

        destroying_slider->set_value(0.75f);
        assert(!tc_ui_document_is_alive(destructive_document.get(), destroying_edit_handle));
        assert(tc_ui_document_live_widget_count(destructive_document.get()) == 0);
        tc_ui_document_destroy(destructive_document_handle);
    }

    void test_combo_box_overlay_selection_and_destruction() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        auto& combo = ui.make_root<ComboBox>();
        combo.add_item("First");
        combo.add_item("Second");
        combo.add_item("Third");
        combo.add_item("Fourth");
        combo.add_item("Fifth");
        combo.add_item("Sixth");
        combo.add_item("Seventh");
        combo.add_item("Eighth");
        combo.add_item("Ninth");
        combo.add_item("Tenth");
        document.layout_roots(tc_ui_rect{10.0f, 10.0f, 180.0f, 34.0f});
        int changes = 0;
        combo.changed().connect([&changes](ComboBox&, int, const std::string&) { ++changes; });

        tc_ui_pointer_event pointer{};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = 20.0f;
        pointer.y = 20.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(combo.open());
        assert(tc_ui_document_overlay_count(document.get()) == 1);
        const tc_widget_handle popup_handle = tc_ui_document_overlay_at(document.get(), 0);
        const tc_widget* popup = tc_ui_document_resolve_widget_const(document.get(), popup_handle);
        assert(popup);
        pointer.type = TC_UI_POINTER_WHEEL;
        pointer.x = popup->bounds.x + 10.0f;
        pointer.y = popup->bounds.y + 10.0f;
        pointer.wheel_y = -1.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = popup->bounds.x + 10.0f;
        pointer.y = popup->bounds.y + 10.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(combo.selected_index() == 2);
        assert(combo.selected_text() == "Third");
        assert(changes == 1);
        assert(!combo.open());
        assert(tc_ui_document_overlay_count(document.get()) == 0);

        assert(tc_ui_document_destroy_widget_recursive(document.get(), combo.handle()));
        assert(!tc_ui_document_is_alive(document.get(), popup_handle));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);

        tc_ui_document_handle destructive_handle = tc_ui_document_create();
        TcDocument destructive_document(destructive_handle);
        install_test_text_measurer(destructive_document);
        DocumentBuilder destructive_ui(destructive_document);
        auto& destructive_combo = destructive_ui.make_root<ComboBox>();
        destructive_combo.add_item("First");
        destructive_combo.add_item("Second");
        destructive_document.layout_roots(tc_ui_rect{10.0f, 10.0f, 180.0f, 34.0f});
        const tc_widget_handle destructive_combo_handle = destructive_combo.handle();
        destructive_combo.changed().connect(
            [&destructive_document, destructive_combo_handle](ComboBox&, int, const std::string&) {
                assert(tc_ui_document_destroy_widget_recursive(destructive_document.get(), destructive_combo_handle));
            });

        pointer = {};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = 20.0f;
        pointer.y = 20.0f;
        assert(destructive_document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        const tc_widget_handle destructive_popup_handle = tc_ui_document_overlay_at(destructive_document.get(), 0);
        const tc_widget* destructive_popup =
            tc_ui_document_resolve_widget_const(destructive_document.get(), destructive_popup_handle);
        assert(destructive_popup);
        pointer.x = destructive_popup->bounds.x + 10.0f;
        pointer.y = destructive_popup->bounds.y + 10.0f;
        assert(destructive_document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(!tc_ui_document_is_alive(destructive_document.get(), destructive_combo_handle));
        assert(!tc_ui_document_is_alive(destructive_document.get(), destructive_popup_handle));
        assert(tc_ui_document_live_widget_count(destructive_document.get()) == 0);
        tc_ui_document_destroy(destructive_handle);
    }

    void test_icon_image_and_canvas_media_contracts() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        install_test_text_measurer(document);
        DocumentBuilder ui(document);
        auto& root = ui.make_root<VStack>("media-root");
        auto& icon = ui.make<IconButton>("I");
        auto& image = ui.make<ImageWidget>();
        auto& canvas = ui.make<Canvas>();
        image.set_texture(41, tc_ui_size{200.0f, 100.0f});
        assert(image.fit() == ImageFit::Contain);
        image.set_fit(ImageFit::Cover);
        assert(image.fit() == ImageFit::Cover);
        canvas.set_texture(42, tc_ui_size{100.0f, 50.0f});
        canvas.set_overlay_texture(43);
        assert(canvas.texture_sampling() == TC_UI_TEXTURE_SAMPLING_LINEAR);
        canvas.set_texture_sampling(TC_UI_TEXTURE_SAMPLING_NEAREST);
        assert(canvas.texture_sampling() == TC_UI_TEXTURE_SAMPLING_NEAREST);
        int custom_paints = 0;
        canvas.set_paint_callback([&custom_paints](Canvas&, tc_ui_paint_context* context) {
            ++custom_paints;
            tc_ui_painter_draw_line(context,
                                    tc_ui_point{0.0f, 0.0f},
                                    tc_ui_point{1.0f, 1.0f},
                                    tc_ui_srgb_color{1.0f, 0.0f, 0.0f, 1.0f},
                                    1.0f);
        });
        root.add_fixed_child(icon, 28.0f);
        root.add_fixed_child(image, 80.0f);
        root.add_fixed_child(canvas, 120.0f);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 240.0f, 240.0f});

        const tc_ui_point center{canvas.bounds().x + canvas.bounds().width * 0.5f,
                                 canvas.bounds().y + canvas.bounds().height * 0.5f};
        canvas.fit_in_view();
        assert(canvas.fit_mode());
        const tc_ui_point image_center = canvas.widget_to_image(center);
        assert(near(image_center.x, 50.0f));
        assert(near(image_center.y, 25.0f));

        tc_ui_pointer_event wheel{};
        wheel.type = TC_UI_POINTER_WHEEL;
        wheel.x = center.x;
        wheel.y = center.y;
        wheel.wheel_y = 1.0f;
        const float old_zoom = canvas.zoom();
        assert(canvas.pointer_event(document.get(), &wheel) == TC_UI_EVENT_HANDLED);
        assert(canvas.zoom() > old_zoom);
        assert(!canvas.fit_mode());
        const tc_ui_point anchored = canvas.widget_to_image(center);
        assert(near(anchored.x, image_center.x));
        assert(near(anchored.y, image_center.y));

        canvas.fit_in_view();
        const float fitted_zoom = canvas.zoom();
        canvas.layout(
            document.get(),
            tc_ui_rect{canvas.bounds().x, canvas.bounds().y, canvas.bounds().width * 0.5f, canvas.bounds().height});
        assert(canvas.fit_mode());
        assert(canvas.zoom() < fitted_zoom);

        int clicks = 0;
        icon.clicked().connect([&clicks](IconButton&) { ++clicks; });
        tc_ui_pointer_event click{};
        click.type = TC_UI_POINTER_DOWN;
        click.x = icon.bounds().x + 4.0f;
        click.y = icon.bounds().y + 4.0f;
        assert(icon.pointer_event(document.get(), &click) == TC_UI_EVENT_HANDLED);
        click.type = TC_UI_POINTER_UP;
        assert(icon.pointer_event(document.get(), &click) == TC_UI_EVENT_HANDLED);
        assert(clicks == 1);

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        assert(custom_paints == 1);
        assert(count_commands(draw_list, TC_UI_DRAW_TEXTURE) == 3);
        size_t linear_textures = 0;
        size_t nearest_textures = 0;
        for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
            if (!command || command->type != TC_UI_DRAW_TEXTURE)
                continue;
            linear_textures += command->texture_sampling == TC_UI_TEXTURE_SAMPLING_LINEAR;
            nearest_textures += command->texture_sampling == TC_UI_TEXTURE_SAMPLING_NEAREST;
        }
        assert(linear_textures == 1);
        assert(nearest_textures == 2);
        assert(count_commands(draw_list, TC_UI_DRAW_LINE) >= 1);
        canvas.clear_texture();
        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);

        assert(tc_ui_document_destroy_widget_recursive(document.get(), root.handle()));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);
    }

} // namespace termin_gui_native_test
