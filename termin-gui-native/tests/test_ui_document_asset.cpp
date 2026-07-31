#include <termin/gui_native/ui_document_asset.hpp>
#include <termin/gui_native/box_layout.hpp>
#include <termin/gui_native/grid_layout.hpp>
#include <termin/gui_native/scroll_area.hpp>
#include <termin/gui_native/label.hpp>
#include <termin/gui_native/wrap_layout.hpp>

#include <cassert>
#include <string>

using namespace termin::gui_native;

namespace {

constexpr const char* SOURCE = R"(
uiscript: 2
root:
  type: termin.gui.BoxLayout
  name: root
  orientation: vertical
  padding: 8
  spacing: 4
  align_items: stretch
  children:
    - type: termin.gui.IconButton
      name: action
      icon: A
      tooltip: Action
      basis: preferred
      grow: 1
      shrink: 0
      min_extent: 24
      max_extent: 80
      align_self: center
)";

constexpr const char* GRID_SCROLL_SOURCE = R"(
uiscript: 2
root:
  type: termin.gui.ScrollArea
  name: scroll
  horizontal_scroll: false
  vertical_scrollbar: always
  children:
    - type: termin.gui.GridLayout
      name: grid
      column_spacing: 4
      row_spacing: 6
      columns:
        - policy: fixed
          value: 40
        - policy: flex
          value: 2
          min_extent: 30
          max_extent: 100
      rows:
        - policy: preferred
          min_extent: 50
        - policy: stretch
          grow: 3
          min_extent: 80
      children:
        - type: termin.gui.Panel
          name: spanning
          row: 0
          column: 0
          column_span: 2
)";

constexpr const char* WRAP_SOURCE = R"(
uiscript: 2
root:
  type: termin.gui.WrapLayout
  name: flow
  orientation: vertical
  spacing: 3
  line_spacing: 5
  line_alignment: end
  children:
    - type: termin.gui.Label
      name: text
      text: Adaptive text
      wrap: character
      overflow: ellipsis
      max_lines: 3
)";

constexpr const char* RESPONSIVE_SOURCE = R"(
uiscript: 2
root:
  type: termin.gui.BoxLayout
  name: responsive
  orientation: vertical
  variants:
    - when: {min_width: 600, orientation: landscape}
      priority: 5
      set: {orientation: horizontal, spacing: 12, safe_area: respect}
)";

void test_compiled_round_trip_and_independent_instances() {
    TcUiDocumentAsset::clear_registry_for_tests();
    TcUiDocumentAsset asset = TcUiDocumentAsset::declare_source(
        "ui-test", "Test UI", "ui/test.uiscript", SOURCE);
    assert(asset.valid());
    const auto recipe = asset.resolve();
    assert(recipe);
    assert(recipe->uuid() == "ui-test");
    assert(recipe->type_dependencies().size() == 2);

    LoadedUiScript first = asset.instantiate();
    LoadedUiScript second = asset.instantiate();
    assert(!(first.document() == second.document()));
    assert(first.named("action").type_name == "termin.gui.IconButton");

    const std::string compiled = recipe->compiled_json();
    TcUiDocumentAsset::clear_registry_for_tests();
    TcUiDocumentAsset restored =
        TcUiDocumentAsset::declare_compiled_json(compiled, "ui-test");
    assert(restored.valid());
    assert(restored.resolve()->compiled_json() == compiled);
    LoadedUiScript loaded = restored.instantiate();
    assert(loaded.named("action").type_name == "termin.gui.IconButton");
    tc_widget* root = tc_ui_document_resolve_widget(
        loaded.document().handle(), loaded.root().handle);
    assert(root);
    const auto* box = static_cast<const BoxLayout*>(root->body);
    assert(box->orientation() == Orientation::Vertical);
    assert(box->items().size() == 1);
    assert(box->items()[0].grow == 1.0f);
    assert(box->items()[0].shrink == 0.0f);
    assert(box->items()[0].min_extent == 24.0f);
    assert(box->items()[0].max_extent == 80.0f);
    assert(box->items()[0].align_self == CrossAxisAlignment::Center);
}

void test_reload_is_transactional_for_recipe_and_instance() {
    TcUiDocumentAsset::clear_registry_for_tests();
    TcUiDocumentAsset asset = TcUiDocumentAsset::declare_source(
        "ui-reload", "Reload UI", "ui/reload.uiscript", SOURCE);
    assert(asset.valid());
    LoadedUiScript loaded = asset.instantiate();
    const std::uint64_t initial_revision = asset.revision();
    const tc_widget_handle old_root = loaded.root().handle;

    assert(!asset.reload_source(
        "uiscript: 2\nroot:\n  type: termin.gui.Missing\n"));
    assert(asset.revision() == initial_revision);
    assert(tc_ui_document_resolve_widget(
        loaded.document().handle(), old_root));
    assert(!asset.reload_source(
        "uiscript: 2\nroot:\n  type: termin.gui.HStack\n"
        "  children:\n    - type: termin.gui.Panel\n"
        "      basis: 20\n      grow: 1\n"));
    assert(asset.revision() == initial_revision);
    assert(tc_ui_document_resolve_widget(
        loaded.document().handle(), old_root));

    std::string replacement = SOURCE;
    const std::size_t name = replacement.find("name: action");
    assert(name != std::string::npos);
    replacement.replace(name, std::string("name: action").size(),
                        "name: replacement");
    assert(asset.reload_source(replacement));
    assert(asset.revision() == initial_revision + 1);
    assert(tc_ui_document_resolve_widget(
        loaded.document().handle(), old_root));

    LoadedUiScript reloaded = asset.reload_instance(loaded);
    assert(!tc_ui_document_resolve_widget(
        reloaded.document().handle(), old_root));
    assert(reloaded.named("replacement").type_name ==
           "termin.gui.IconButton");
}

void test_generation_handle_and_compiled_validation() {
    TcUiDocumentAsset::clear_registry_for_tests();
    TcUiDocumentAsset asset = TcUiDocumentAsset::declare_source(
        "ui-generation", "Generation UI", "ui/generation.uiscript", SOURCE);
    assert(asset.valid());
    const UiDocumentAssetHandle stale = asset.handle();
    std::string compiled = asset.resolve()->compiled_json();
    assert(asset.remove());
    assert(!stale.valid());
    assert(!TcUiDocumentAsset(stale).valid());

    const std::size_t dependency = compiled.find("termin.gui.BoxLayout");
    assert(dependency != std::string::npos);
    compiled.replace(
        dependency, std::string("termin.gui.BoxLayout").size(),
        "termin.gui.Missing");
    assert(!TcUiDocumentAsset::declare_compiled_json(
        compiled, "ui-generation").valid());
}

void test_grid_scroll_round_trip_and_transactional_reload() {
    TcUiDocumentAsset::clear_registry_for_tests();
    TcUiDocumentAsset asset = TcUiDocumentAsset::declare_source(
        "ui-grid-scroll", "Grid Scroll", "ui/grid-scroll.uiscript",
        GRID_SCROLL_SOURCE);
    assert(asset.valid());
    assert(asset.resolve()->type_dependencies().size() == 3);
    const std::string compiled = asset.resolve()->compiled_json();
    const std::uint64_t revision = asset.revision();
    LoadedUiScript instance = asset.instantiate();
    const tc_widget_handle old_root = instance.root().handle;

    TcUiDocumentAsset::clear_registry_for_tests();
    asset = TcUiDocumentAsset::declare_compiled_json(
        compiled, "ui-grid-scroll");
    assert(asset.valid());
    assert(asset.resolve()->compiled_json() == compiled);
    LoadedUiScript restored = asset.instantiate();
    tc_widget* scroll_widget = tc_ui_document_resolve_widget(
        restored.document().handle(), restored.root().handle);
    tc_widget* grid_widget = tc_ui_document_resolve_widget(
        restored.document().handle(), restored.named("grid").handle);
    assert(scroll_widget && grid_widget);
    const auto* scroll = static_cast<const ScrollArea*>(scroll_widget->body);
    const auto* grid = static_cast<const GridLayout*>(grid_widget->body);
    assert(!scroll->horizontal_scroll_enabled());
    assert(scroll->vertical_scrollbar_policy() == ScrollBarPolicy::Always);
    assert(grid->columns()[1].policy == LayoutPolicy::Flex);
    assert(grid->columns()[1].value == 2.0f);
    assert(grid->columns()[1].min_extent == 30.0f);
    assert(grid->rows()[1].grow == 3.0f);
    assert(grid->items()[0].column_span == 2);

    LoadedUiScript live = asset.instantiate();
    const tc_widget_handle live_root = live.root().handle;
    assert(!asset.reload_source(
        "uiscript: 2\nroot:\n  type: termin.gui.ScrollArea\n"
        "  children:\n    - type: termin.gui.Panel\n"
        "    - type: termin.gui.Panel\n"));
    assert(asset.revision() == revision);
    assert(tc_ui_document_resolve_widget(
        live.document().handle(), live_root));
    assert(tc_ui_document_resolve_widget(
        instance.document().handle(), old_root));
}

void test_wrapped_label_package_round_trip() {
    TcUiDocumentAsset::clear_registry_for_tests();
    TcUiDocumentAsset asset = TcUiDocumentAsset::declare_source(
        "ui-wrap", "Wrapped UI", "ui/wrap.uiscript", WRAP_SOURCE);
    assert(asset.valid());
    const std::string compiled = asset.resolve()->compiled_json();
    TcUiDocumentAsset::clear_registry_for_tests();
    asset = TcUiDocumentAsset::declare_compiled_json(compiled, "ui-wrap");
    assert(asset.valid());
    assert(asset.resolve()->compiled_json() == compiled);
    LoadedUiScript restored = asset.instantiate();
    tc_widget* flow_widget = tc_ui_document_resolve_widget(
        restored.document().handle(), restored.root().handle);
    tc_widget* label_widget = tc_ui_document_resolve_widget(
        restored.document().handle(), restored.named("text").handle);
    assert(flow_widget && label_widget);
    const auto* flow = static_cast<const WrapLayout*>(flow_widget->body);
    const auto* label = static_cast<const Label*>(label_widget->body);
    assert(flow->orientation() == Orientation::Vertical);
    assert(flow->line_spacing() == 5.0f);
    assert(flow->line_alignment() == CrossAxisAlignment::End);
    assert(label->wrap_mode() == TextWrapMode::Character);
    assert(label->overflow() == TextOverflow::Ellipsis);
    assert(label->max_lines() == 3);
}

void test_responsive_variant_package_round_trip() {
    TcUiDocumentAsset::clear_registry_for_tests();
    TcUiDocumentAsset asset = TcUiDocumentAsset::declare_source(
        "ui-responsive", "Responsive UI",
        "ui/responsive.uiscript", RESPONSIVE_SOURCE);
    assert(asset.valid());
    const std::string compiled = asset.resolve()->compiled_json();
    assert(compiled.find("\"variants\"") != std::string::npos);
    TcUiDocumentAsset::clear_registry_for_tests();
    asset = TcUiDocumentAsset::declare_compiled_json(
        compiled, "ui-responsive");
    assert(asset.valid());
    assert(asset.resolve()->compiled_json() == compiled);
    LoadedUiScript restored = asset.instantiate();
    restored.document().layout_roots({0.0f, 0.0f, 800.0f, 600.0f});
    tc_widget* root = tc_ui_document_resolve_widget(
        restored.document().handle(), restored.root().handle);
    assert(root);
    const auto* box = static_cast<const BoxLayout*>(root->body);
    assert(box->orientation() == Orientation::Horizontal);
    assert(box->spacing() == 12.0f);
    assert(restored.document().root_layout_policy() ==
           TC_UI_ROOT_LAYOUT_SAFE_AREA);
}

} // namespace

int main() {
    test_compiled_round_trip_and_independent_instances();
    test_reload_is_transactional_for_recipe_and_instance();
    test_generation_handle_and_compiled_validation();
    test_grid_scroll_round_trip_and_transactional_reload();
    test_wrapped_label_package_round_trip();
    test_responsive_variant_package_round_trip();
    TcUiDocumentAsset::clear_registry_for_tests();
    return 0;
}
