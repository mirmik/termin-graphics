#include <termin/gui_native/uiscript.hpp>
#include <termin/gui_native/box_layout.hpp>
#include <termin/gui_native/grid_layout.hpp>
#include <termin/gui_native/scroll_area.hpp>
#include <termin/gui_native/wrap_layout.hpp>

#include <cassert>
#include <string>

#include "widgets_test_support.hpp"

using namespace termin::gui_native;

namespace {

constexpr const char* SCRIPT = R"(
uiscript: 2
root:
  type: termin.gui.OverlayLayout
  name: overlay
  background_color: [0, 0, 0, 0]
  children:
    - type: termin.gui.HStack
      name: controls
      anchor: top-right
      offset: [-10, 10]
      spacing: 4
      children:
        - type: termin.gui.Label
          name: title
          text: Native HUD
          font_size: 18
          color: [0.9, 0.95, 1, 1]
        - type: termin.gui.IconButton
          name: inspect_btn
          icon: I
          tooltip: Inspect
          size: 26
          active_color: [0.2, 0.5, 0.8, 0.95]
)";

void test_parse_and_independent_materialization() {
    UiScriptLoader loader;
    const UiScriptDescription description = loader.parser.parse(SCRIPT);
    assert(description.version == 2);
    assert(description.root.type_name == "termin.gui.OverlayLayout");
    assert(description.root.children.size() == 1);
    assert(description.type_dependencies.size() == 4);

    LoadedUiScript first = loader.materialize(description);
    LoadedUiScript second = loader.materialize(description);
    termin_gui_native_test::install_test_text_measurer(first.document());
    termin_gui_native_test::install_test_text_measurer(second.document());
    assert(first.document().valid());
    assert(second.document().valid());
    assert(!(first.document() == second.document()));
    assert(first.named("inspect_btn").type_name == "termin.gui.IconButton");
    assert(first.named("title").type_name == "termin.gui.Label");
    assert(
        tc_ui_document_resolve_widget(
            first.document().handle(), first.named("inspect_btn").handle) !=
        tc_ui_document_resolve_widget(
            second.document().handle(), second.named("inspect_btn").handle));

    first.document().layout_roots({0.0f, 0.0f, 800.0f, 600.0f});
    tc_widget* button = tc_ui_document_resolve_widget(
        first.document().handle(), first.named("inspect_btn").handle);
    assert(button);
    const tc_ui_rect bounds = tc_widget_bounds(button);
    assert(bounds.x == 764.0f);
    assert(bounds.y == 10.0f);

    tc_ui_pointer_event covered{};
    covered.type = TC_UI_POINTER_DOWN;
    covered.x = bounds.x + bounds.width * 0.5f;
    covered.y = bounds.y + bounds.height * 0.5f;
    assert(
        tc_ui_document_dispatch_pointer_event(
            first.document().handle(), &covered) == TC_UI_EVENT_HANDLED);

    tc_ui_pointer_event uncovered{};
    uncovered.type = TC_UI_POINTER_DOWN;
    uncovered.x = 100.0f;
    uncovered.y = 500.0f;
    assert(
        tc_ui_document_dispatch_pointer_event(
            first.document().handle(), &uncovered) == TC_UI_EVENT_IGNORED);
}

void test_structural_diagnostics() {
    UiScriptLoader loader;
    try {
        loader.parser.parse(
            "uiscript: 2\n"
            "root:\n"
            "  type: termin.gui.Panel\n"
            "  children:\n"
            "    - type: termin.gui.IconButton\n"
            "      name: duplicate\n"
            "    - type: termin.gui.IconButton\n"
            "      name: duplicate\n");
        assert(false);
    } catch (const UiScriptError& error) {
        assert(std::string(error.what()).find("root.children[1].name") !=
               std::string::npos);
    }
}

void test_materialization_rollback() {
    UiScriptLoader loader;
    UiScriptDescription description = loader.parser.parse(SCRIPT);
    description.root.children[0].children[0].type_name =
        "termin.gui.MissingWidget";

    const tc_ui_document_handle handle = tc_ui_document_create();
    try {
        loader.materialize(description, TcDocument(handle));
        assert(false);
    } catch (const UiScriptError&) {
    }
    assert(tc_ui_document_live_widget_count(handle) == 0);
    assert(tc_ui_document_root_count(handle) == 0);
    tc_ui_document_destroy(handle);
}

void test_generic_layout_spec_validation_and_materialization() {
    UiScriptLoader loader;
    LoadedUiScript loaded = loader.load_string(
        "uiscript: 2\n"
        "root:\n"
        "  type: termin.gui.Panel\n"
        "  name: adaptive_panel\n"
        "  layout:\n"
        "    width: 50%\n"
        "    height: fill\n"
        "    min_width: 120\n"
        "    max_width: 360\n"
        "    margin: [4, 8, 12, 16]\n"
        "    minimum_touch_target: [48, 44]\n");
    tc_widget* widget = tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.root().handle);
    assert(widget);
    const tc_ui_widget_layout_spec spec = tc_widget_layout_spec(widget);
    assert(spec.width.mode == TC_UI_LENGTH_PERCENT);
    assert(spec.width.value == 0.5f);
    assert(spec.height.mode == TC_UI_LENGTH_FILL);
    assert(spec.min_width == 120.0f && spec.max_width == 360.0f);
    assert(spec.margin.left == 4.0f && spec.margin.bottom == 16.0f);
    assert(spec.touch_target_policy == TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM);
    assert(spec.minimum_touch_target.width == 48.0f);
    assert(loaded.root().properties["layout"]["width"].as_string() == "50%");

    const std::vector<std::string> invalid{
        "    width: 110%\n",
        "    min_width: 20\n    max_width: 10\n",
        "    width: 10\n    height: 10\n    aspect_ratio: 2\n",
        "    minimum_touch_target: true\n",
        "    max_width: 40\n    minimum_touch_target: [48, 44]\n",
        "    mystery: 1\n",
    };
    for (const std::string& layout : invalid) {
        try {
            loader.parser.parse(
                "uiscript: 2\nroot:\n  type: termin.gui.Panel\n  layout:\n" +
                layout);
            assert(false);
        } catch (const UiScriptError& error) {
            assert(std::string(error.what()).find("root.layout") != std::string::npos);
        }
    }
}

void test_box_properties_placement_and_strict_validation() {
    UiScriptLoader loader;
    LoadedUiScript loaded = loader.load_string(
        "uiscript: 2\n"
        "root:\n"
        "  type: termin.gui.BoxLayout\n"
        "  name: box\n"
        "  orientation: vertical\n"
        "  padding: [10, 5, 20, 7]\n"
        "  spacing: 3\n"
        "  align_items: center\n"
        "  children:\n"
        "    - type: termin.gui.IconButton\n"
        "      name: fixed\n"
        "      icon: F\n"
        "      size: 10\n"
        "      basis: 20\n"
        "    - type: termin.gui.IconButton\n"
        "      name: capped\n"
        "      icon: C\n"
        "      size: 10\n"
        "      basis: preferred\n"
        "      grow: 1\n"
        "      shrink: 0\n"
        "      max_extent: 50\n"
        "      align_self: end\n"
        "    - type: termin.gui.IconButton\n"
        "      name: flexible\n"
        "      icon: X\n"
        "      size: 10\n"
        "      grow: 2\n"
        "      shrink: 0\n"
        "      align_self: stretch\n");
    termin_gui_native_test::install_test_text_measurer(loaded.document());
    loaded.document().layout_roots({0.0f, 0.0f, 100.0f, 200.0f});

    tc_widget* root_widget = tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.root().handle);
    assert(root_widget);
    const auto* box = static_cast<const BoxLayout*>(root_widget->body);
    assert(box->orientation() == Orientation::Vertical);
    assert(box->padding().left == 10.0f && box->padding().bottom == 7.0f);
    assert(box->spacing() == 3.0f);
    assert(box->cross_axis_alignment() == CrossAxisAlignment::Center);
    assert(box->items().size() == 3);
    assert(box->items()[0].policy == LayoutPolicy::Fixed);
    assert(box->items()[1].grow == 1.0f &&
           box->items()[1].max_extent == 50.0f);
    assert(box->items()[2].grow == 2.0f);

    const tc_ui_rect fixed = tc_widget_bounds(tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.named("fixed").handle));
    const tc_ui_rect capped = tc_widget_bounds(tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.named("capped").handle));
    const tc_ui_rect flexible = tc_widget_bounds(tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.named("flexible").handle));
    assert(fixed.x == 40.0f && fixed.y == 5.0f &&
           fixed.width == 10.0f && fixed.height == 20.0f);
    assert(capped.x == 70.0f && capped.y == 28.0f &&
           capped.width == 10.0f && capped.height == 50.0f);
    assert(flexible.x == 10.0f && flexible.y == 81.0f &&
           flexible.width == 70.0f && flexible.height == 112.0f);

    const std::vector<std::string> invalid{
        "      basis: 20\n      grow: 1\n",
        "      min_extent: 30\n      max_extent: 20\n",
    };
    for (const std::string& placement : invalid) {
        try {
            loader.parser.parse(
                "uiscript: 2\nroot:\n  type: termin.gui.HStack\n"
                "  children:\n    - type: termin.gui.Panel\n" +
                placement);
            assert(false);
        } catch (const UiScriptError& error) {
            assert(std::string(error.what()).find("root.children[0]") !=
                   std::string::npos);
        }
    }
    try {
        loader.parser.parse(
            "uiscript: 2\nroot:\n  type: termin.gui.Panel\n  grow: 1\n");
        assert(false);
    } catch (const UiScriptError& error) {
        assert(std::string(error.what()).find(
                   "unsupported termin.gui.Panel property 'grow'") !=
               std::string::npos);
    }
}

void test_grid_and_scroll_facets() {
    UiScriptLoader loader;
    LoadedUiScript loaded = loader.load_string(
        "uiscript: 2\n"
        "root:\n"
        "  type: termin.gui.ScrollArea\n"
        "  name: scroll\n"
        "  horizontal_scroll: false\n"
        "  vertical_scroll: true\n"
        "  horizontal_scrollbar: hidden\n"
        "  vertical_scrollbar: always\n"
        "  children:\n"
        "    - type: termin.gui.GridLayout\n"
        "      name: grid\n"
        "      padding: [2, 3, 4, 5]\n"
        "      column_spacing: 6\n"
        "      row_spacing: 4\n"
        "      columns:\n"
        "        - policy: fixed\n"
        "          value: 30\n"
        "        - policy: flex\n"
        "          value: 2\n"
        "          grow: 3\n"
        "          shrink: 1\n"
        "          min_extent: 20\n"
        "          max_extent: 100\n"
        "      rows:\n"
        "        - policy: fixed\n"
        "          value: 40\n"
        "        - policy: preferred\n"
        "          min_extent: 60\n"
        "        - policy: stretch\n"
        "          min_extent: 80\n"
        "          max_extent: 120\n"
        "      children:\n"
        "        - type: termin.gui.Panel\n"
        "          name: header\n"
        "          row: 0\n"
        "          column: 0\n"
        "          column_span: 2\n"
        "        - type: termin.gui.IconButton\n"
        "          name: bottom\n"
        "          icon: B\n"
        "          row: 2\n"
        "          column: 1\n");
    termin_gui_native_test::install_test_text_measurer(loaded.document());
    loaded.document().layout_roots({0.0f, 0.0f, 120.0f, 70.0f});

    tc_widget* scroll_widget = tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.root().handle);
    tc_widget* grid_widget = tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.named("grid").handle);
    assert(scroll_widget && grid_widget);
    auto* scroll = static_cast<ScrollArea*>(scroll_widget->body);
    const auto* grid = static_cast<const GridLayout*>(grid_widget->body);
    assert(!scroll->horizontal_scroll_enabled());
    assert(scroll->vertical_scroll_enabled());
    assert(scroll->horizontal_scrollbar_policy() == ScrollBarPolicy::Hidden);
    assert(scroll->vertical_scrollbar_policy() == ScrollBarPolicy::Always);
    assert(grid->columns().size() == 2 && grid->rows().size() == 3);
    assert(grid->columns()[1].policy == LayoutPolicy::Flex);
    assert(grid->columns()[1].value == 2.0f);
    assert(grid->columns()[1].grow == 3.0f);
    assert(grid->columns()[1].shrink == 1.0f);
    assert(grid->columns()[1].min_extent == 20.0f);
    assert(grid->columns()[1].max_extent == 100.0f);
    assert(grid->items().size() == 2);
    assert(grid->items()[0].column_span == 2);
    assert(scroll->content_size().width == 120.0f);
    assert(scroll->content_size().height == 196.0f);

    const tc_widget_handle bottom = loaded.named("bottom").handle;
    assert(scroll->ensure_visible(bottom));
    assert(scroll->scroll_y() > 0.0f);
    const tc_ui_rect narrow_bottom = tc_widget_bounds(
        tc_ui_document_resolve_widget(loaded.document().handle(), bottom));
    loaded.document().layout_roots({0.0f, 0.0f, 180.0f, 100.0f});
    const tc_ui_rect wide_bottom = tc_widget_bounds(
        tc_ui_document_resolve_widget(loaded.document().handle(), bottom));
    assert(scroll->content_size().width == 180.0f);
    assert(scroll->content_size().height == 196.0f);
    assert(scroll->scroll_y() == 96.0f);
    assert(wide_bottom.width > narrow_bottom.width);

    const std::vector<std::string> invalid{
        "  type: termin.gui.GridLayout\n"
        "  columns:\n"
        "    - policy: fixed\n"
        "  rows:\n"
        "    - policy: stretch\n",
        "  type: termin.gui.GridLayout\n"
        "  columns:\n"
        "    - policy: stretch\n"
        "  rows:\n"
        "    - policy: stretch\n"
        "  children:\n"
        "    - type: termin.gui.Panel\n"
        "      row: 0\n"
        "      column: 0\n"
        "      column_span: 2\n",
        "  type: termin.gui.ScrollArea\n"
        "  children:\n"
        "    - type: termin.gui.Panel\n"
        "    - type: termin.gui.Panel\n",
    };
    for (const std::string& root : invalid) {
        try {
            loader.parser.parse("uiscript: 2\nroot:\n" + root);
            assert(false);
        } catch (const UiScriptError& error) {
            assert(std::string(error.what()).find("root") != std::string::npos);
        }
    }
}

void test_wrapped_label_and_wrap_layout_facets() {
    UiScriptLoader loader;
    LoadedUiScript loaded = loader.load_string(
        "uiscript: 2\n"
        "root:\n"
        "  type: termin.gui.WrapLayout\n"
        "  name: flow\n"
        "  orientation: horizontal\n"
        "  padding: 2\n"
        "  spacing: 3\n"
        "  line_spacing: 4\n"
        "  line_alignment: center\n"
        "  children:\n"
        "    - type: termin.gui.Label\n"
        "      name: wrapped\n"
        "      text: one two three\n"
        "      font_size: 10\n"
        "      wrap: word\n"
        "      overflow: ellipsis\n"
        "      max_lines: 2\n");
    termin_gui_native_test::install_test_text_measurer(loaded.document());
    loaded.document().layout_roots({0.0f, 0.0f, 45.0f, 80.0f});

    tc_widget* root_widget = tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.root().handle);
    tc_widget* label_widget = tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.named("wrapped").handle);
    assert(root_widget && label_widget);
    const auto* flow = static_cast<const WrapLayout*>(root_widget->body);
    const auto* label = static_cast<const Label*>(label_widget->body);
    assert(flow->orientation() == Orientation::Horizontal);
    assert(flow->padding().left == 2.0f);
    assert(flow->spacing() == 3.0f);
    assert(flow->line_spacing() == 4.0f);
    assert(flow->line_alignment() == CrossAxisAlignment::Center);
    assert(label->wrap_mode() == TextWrapMode::Word);
    assert(label->overflow() == TextOverflow::Ellipsis);
    assert(label->max_lines() == 2);
    assert(loaded.named("wrapped").properties["wrap"].as_string() == "word");

    const std::vector<std::string> invalid{
        "  type: termin.gui.Label\n  wrap: anywhere\n",
        "  type: termin.gui.Label\n  overflow: fade\n",
        "  type: termin.gui.Label\n  max_lines: -1\n",
        "  type: termin.gui.WrapLayout\n  line_alignment: auto\n",
        "  type: termin.gui.WrapLayout\n  line_spacing: nope\n",
    };
    for (const std::string& root : invalid) {
        try {
            loader.parser.parse("uiscript: 2\nroot:\n" + root);
            assert(false);
        } catch (const UiScriptError& error) {
            assert(std::string(error.what()).find("root") !=
                   std::string::npos);
        }
    }
}

} // namespace

int main() {
    test_parse_and_independent_materialization();
    test_structural_diagnostics();
    test_materialization_rollback();
    test_generic_layout_spec_validation_and_materialization();
    test_box_properties_placement_and_strict_validation();
    test_grid_and_scroll_facets();
    test_wrapped_label_and_wrap_layout_facets();
    return 0;
}
