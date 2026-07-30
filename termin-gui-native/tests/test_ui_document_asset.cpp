#include <termin/gui_native/ui_document_asset.hpp>

#include <cassert>
#include <string>

using namespace termin::gui_native;

namespace {

constexpr const char* SOURCE = R"(
uiscript: 2
root:
  type: termin.gui.Panel
  name: root
  background_color: [0.1, 0.2, 0.3, 1]
  children:
    - type: termin.gui.IconButton
      name: action
      icon: A
      tooltip: Action
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

    const std::size_t dependency = compiled.find("termin.gui.Panel");
    assert(dependency != std::string::npos);
    compiled.replace(
        dependency, std::string("termin.gui.Panel").size(),
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
