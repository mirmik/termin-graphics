#include <termin/gui_native/showcase.hpp>

#include <termin/gui_native/widgets.hpp>

namespace termin::gui_native {

ShowcaseRefs build_showcase(Document& document) {
    DocumentBuilder ui(document);
    ShowcaseRefs refs;

    auto& root = ui.make_root<VStack>("showcase-root");
    root.set_padding(EdgeInsets {18.0f, 18.0f, 18.0f, 18.0f})
        .set_spacing(14.0f)
        .set_background(Color {0.055f, 0.060f, 0.070f, 1.0f});

    auto& top = ui.make<HStack>("showcase-top");
    top.set_spacing(12.0f);
    root.add_child(top);

    auto& navigation = ui.make<VStack>("navigation");
    navigation.set_padding(EdgeInsets {12.0f, 12.0f, 12.0f, 12.0f})
        .set_spacing(8.0f)
        .set_background(Color {0.10f, 0.11f, 0.13f, 1.0f})
        .set_border(Color {0.28f, 0.30f, 0.34f, 1.0f}, 1.0f);
    top.add_child(navigation);
    navigation.add_child(ui.make<Label>("Native UI", 18.0f, Color {0.92f, 0.95f, 1.0f, 1.0f}));
    navigation.add_child(ui.make<Button>("Scene", Color {0.18f, 0.32f, 0.54f, 1.0f}));
    navigation.add_child(ui.make<Button>("Assets", Color {0.16f, 0.42f, 0.32f, 1.0f}));
    navigation.add_child(ui.make<Button>("Build", Color {0.48f, 0.28f, 0.20f, 1.0f}));

    refs.content_scroll = &ui.make<ScrollArea>("content-scroll");
    top.add_child(*refs.content_scroll);

    auto& content = ui.make<VStack>("content");
    content.set_padding(EdgeInsets {14.0f, 14.0f, 14.0f, 14.0f})
        .set_spacing(10.0f)
        .set_background(Color {0.13f, 0.14f, 0.16f, 1.0f})
        .set_border(Color {0.36f, 0.39f, 0.44f, 1.0f}, 1.0f);
    refs.content_scroll->set_content(content);

    auto& preview_split = ui.make<Splitter>(Orientation::Horizontal, "preview-split");
    preview_split.set_split_fraction(0.58f).set_min_extents(96.0f, 96.0f).set_divider_thickness(8.0f);
    content.add_child(preview_split);
    preview_split.set_first(ui.make<Panel>("preview-a").set_fill(Color {0.20f, 0.42f, 0.62f, 1.0f}));
    preview_split.set_second(ui.make<Panel>("preview-b").set_fill(Color {0.26f, 0.48f, 0.34f, 1.0f}));

    content.add_preferred_child(ui.make<Separator>(Orientation::Horizontal));

    auto& controls_group = ui.make<GroupBox>("Controls", "controls-group");
    content.add_preferred_child(controls_group);

    auto& controls = ui.make<HStack>("controls");
    controls.set_spacing(10.0f);
    controls_group.set_content(controls);
    refs.checkbox = &ui.make<Checkbox>(true);
    refs.slider = &ui.make<Slider>(0.62f);
    refs.progress = &ui.make<ProgressBar>(0.35f);
    refs.text_input = &ui.make<TextInput>("Scene 01");
    controls.add_child(ui.make<Label>("Live", 14.0f, Color {0.78f, 0.84f, 0.92f, 1.0f}));
    controls.add_child(*refs.checkbox);
    controls.add_child(*refs.slider);
    controls.add_child(*refs.progress);
    controls.add_child(*refs.text_input);
    refs.slider->changed().connect([progress = refs.progress](Slider&, float value) {
        progress->set_value(value);
    });

    refs.text_area = &ui.make<TextArea>(
        "Native TextArea\nUTF-8 selection and host clipboard\nHorizontal and vertical scrolling"
    );
    content.add_fixed_child(*refs.text_area, 96.0f);

    auto& palette = ui.make<GridLayout>("palette-grid");
    palette.set_padding(EdgeInsets {8.0f, 8.0f, 8.0f, 8.0f})
        .set_spacing(8.0f, 8.0f)
        .set_background(Color {0.10f, 0.11f, 0.13f, 1.0f})
        .set_border(Color {0.30f, 0.32f, 0.36f, 1.0f}, 1.0f);
    palette.add_row(LayoutPolicy::Preferred);
    palette.add_column(LayoutPolicy::Stretch);
    palette.add_column(LayoutPolicy::Stretch);
    palette.add_column(LayoutPolicy::Stretch);
    palette.add_column(LayoutPolicy::Stretch);
    content.add_preferred_child(palette);
    palette.add_child(ui.make<Swatch>(Color {0.90f, 0.22f, 0.24f, 1.0f}), 0, 0);
    palette.add_child(ui.make<Swatch>(Color {0.95f, 0.72f, 0.28f, 1.0f}), 0, 1);
    palette.add_child(ui.make<Swatch>(Color {0.24f, 0.68f, 0.48f, 1.0f}), 0, 2);
    palette.add_child(ui.make<Swatch>(Color {0.28f, 0.52f, 0.92f, 1.0f}), 0, 3);

    refs.tabs = &ui.make<TabView>("bottom-tabs");
    root.add_child(*refs.tabs);
    refs.tabs->add_page("Status", ui.make<Panel>("status-a").set_fill(Color {0.12f, 0.20f, 0.26f, 1.0f}));
    refs.tabs->add_page("Output", ui.make<Panel>("status-b").set_fill(Color {0.22f, 0.17f, 0.28f, 1.0f}));

    return refs;
}

} // namespace termin::gui_native
