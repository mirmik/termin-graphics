#include <termin/gui_native/uiscript.hpp>

#include <cassert>
#include <string>

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
    assert(description.type_dependencies.size() == 3);

    LoadedUiScript first = loader.materialize(description);
    LoadedUiScript second = loader.materialize(description);
    assert(first.document().valid());
    assert(second.document().valid());
    assert(!(first.document() == second.document()));
    assert(first.named("inspect_btn").type_name == "termin.gui.IconButton");
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

} // namespace

int main() {
    test_parse_and_independent_materialization();
    test_structural_diagnostics();
    test_materialization_rollback();
    return 0;
}
