#include <termin/gui_native/ui_document_asset.hpp>
#include <termin/gui_native/box_layout.hpp>

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

} // namespace

int main() {
    test_compiled_round_trip_and_independent_instances();
    test_reload_is_transactional_for_recipe_and_instance();
    test_generation_handle_and_compiled_validation();
    TcUiDocumentAsset::clear_registry_for_tests();
    return 0;
}
