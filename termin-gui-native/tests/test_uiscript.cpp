#include <termin/gui_native/uiscript.hpp>

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

} // namespace

int main() {
    test_parse_and_independent_materialization();
    test_structural_diagnostics();
    test_materialization_rollback();
    test_generic_layout_spec_validation_and_materialization();
    return 0;
}
