#include <termin/gui_native/ui_document_asset.hpp>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <tcbase/tc_log.h>
#include <tcbase/tc_trent_json.hpp>

namespace termin::gui_native {
namespace {

struct AssetSlot {
    std::uint32_t generation = 1;
    std::shared_ptr<const UiDocumentAsset> asset;
};

std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<AssetSlot>& registry_slots() {
    static std::vector<AssetSlot> slots;
    return slots;
}

std::unordered_map<std::string, std::uint32_t>& registry_uuids() {
    static std::unordered_map<std::string, std::uint32_t> uuids;
    return uuids;
}

std::shared_ptr<const UiDocumentAsset> resolve_locked(
    UiDocumentAssetHandle handle
) {
    const auto& slots = registry_slots();
    if (handle.index >= slots.size()) {
        return {};
    }
    const AssetSlot& slot = slots[handle.index];
    if (slot.generation != handle.generation) {
        return {};
    }
    return slot.asset;
}

tc::trent encode_node(const UiScriptNode& node) {
    tc::trent encoded = tc::trent::dict();
    encoded.set("type", node.type_name);
    if (!node.name.empty()) {
        encoded.set("name", node.name);
    }
    for (const auto property : node.properties.as_dict()) {
        encoded.set(
            property.key ? property.key : "",
            tc::trent_view(property.value));
    }
    if (!node.children.empty()) {
        tc::trent children = tc::trent::list();
        for (const UiScriptNode& child : node.children) {
            children.push_back(encode_node(child));
        }
        encoded.set("children", std::move(children));
    }
    return encoded;
}

tc::trent encode_recipe(const UiScriptDescription& description) {
    tc::trent recipe = tc::trent::dict();
    recipe.set("uiscript", static_cast<std::int64_t>(description.version));
    recipe.set("root", encode_node(description.root));
    return recipe;
}

std::vector<std::string> string_list(
    tc::trent_view value,
    const std::string& path
) {
    if (!value.is_list()) {
        throw UiScriptError(path + ": expected a list");
    }
    std::vector<std::string> result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!value[index].is_string() || value[index].as_string().empty()) {
            throw UiScriptError(
                path + "[" + std::to_string(index) +
                "]: expected a non-empty string");
        }
        result.push_back(value[index].as_string());
    }
    return result;
}

std::shared_ptr<UiDocumentAsset> build_source_asset(
    std::string uuid,
    std::string name,
    std::string source_identity,
    const std::string& source,
    std::uint64_t revision
) {
    if (uuid.empty()) {
        throw UiScriptError("ui document asset UUID must not be empty");
    }
    UiScriptDescription description;
    try {
        description = UiScriptParser().parse(source);
    } catch (const std::exception& error) {
        throw UiScriptError(
            (source_identity.empty() ? std::string("<memory>") : source_identity) +
            ": " + error.what());
    }
    const std::string asset_name = name.empty() ? uuid : std::move(name);
    return std::make_shared<UiDocumentAsset>(
        std::move(uuid), asset_name, std::move(source_identity), revision,
        std::move(description));
}

std::shared_ptr<UiDocumentAsset> build_compiled_asset(
    const std::string& compiled_json,
    const std::string& expected_uuid
) {
    tc::trent payload;
    try {
        payload = tc::json::parse(compiled_json);
    } catch (const std::exception& error) {
        throw UiScriptError(
            std::string("invalid compiled UI document JSON: ") + error.what());
    }
    if (!payload.is_dict()) {
        throw UiScriptError("compiled_ui_document: expected a mapping");
    }
    const tc::trent_view schema = payload["ui_document_asset"];
    if (!schema.is_integer() ||
        schema.as_integer() != UI_DOCUMENT_ASSET_SCHEMA_VERSION) {
        throw UiScriptError(
            "compiled_ui_document.ui_document_asset: expected schema version " +
            std::to_string(UI_DOCUMENT_ASSET_SCHEMA_VERSION));
    }
    const tc::trent_view uuid = payload["uuid"];
    if (!uuid.is_string() || uuid.as_string().empty()) {
        throw UiScriptError(
            "compiled_ui_document.uuid: expected a non-empty string");
    }
    if (!expected_uuid.empty() && uuid.as_string() != expected_uuid) {
        throw UiScriptError(
            "compiled_ui_document.uuid: manifest UUID '" + expected_uuid +
            "' does not match payload UUID '" + uuid.as_string() + "'");
    }
    const tc::trent_view recipe = payload["recipe"];
    if (!recipe.is_dict()) {
        throw UiScriptError(
            "compiled_ui_document.recipe: expected a mapping");
    }

    const std::string asset_uuid = uuid.as_string();
    const std::string asset_name = payload["name"].is_string()
        ? payload["name"].as_string()
        : asset_uuid;
    const std::string source_identity = payload["source_identity"].is_string()
        ? payload["source_identity"].as_string()
        : std::string{};
    const tc::trent_view revision = payload["revision"];
    if (!revision.is_integer() || revision.as_integer() <= 0) {
        throw UiScriptError(
            "compiled_ui_document.revision: expected an integer >= 1");
    }
    UiScriptDescription description =
        UiScriptParser().parse_document(recipe);

    const std::vector<std::string> encoded_dependencies = string_list(
        payload["type_dependencies"],
        "compiled_ui_document.type_dependencies");
    if (encoded_dependencies != description.type_dependencies) {
        throw UiScriptError(
            "compiled_ui_document.type_dependencies: payload does not match recipe");
    }
    return std::make_shared<UiDocumentAsset>(
        asset_uuid, asset_name, source_identity,
        static_cast<std::uint64_t>(revision.as_integer()),
        std::move(description));
}

TcUiDocumentAsset register_asset(
    std::shared_ptr<const UiDocumentAsset> asset
) {
    std::lock_guard lock(registry_mutex());
    auto& slots = registry_slots();
    auto& uuids = registry_uuids();
    const auto found = uuids.find(asset->uuid());
    if (found != uuids.end()) {
        const AssetSlot& slot = slots[found->second];
        if (!slot.asset ||
            slot.asset->compiled_json(-1) != asset->compiled_json(-1)) {
            tc_log_error(
                "[gui-native-ui-asset] UUID collision for '%s'",
                asset->uuid().c_str());
            return {};
        }
        return TcUiDocumentAsset(
            UiDocumentAssetHandle{found->second, slot.generation});
    }

    std::uint32_t index = 0;
    for (; index < slots.size(); ++index) {
        if (!slots[index].asset) {
            break;
        }
    }
    if (index == slots.size()) {
        slots.push_back({});
    }
    AssetSlot& slot = slots[index];
    slot.asset = std::move(asset);
    uuids.emplace(slot.asset->uuid(), index);
    return TcUiDocumentAsset(
        UiDocumentAssetHandle{index, slot.generation});
}

} // namespace

bool UiDocumentAssetHandle::valid() const {
    std::lock_guard lock(registry_mutex());
    return static_cast<bool>(resolve_locked(*this));
}

UiDocumentAsset::UiDocumentAsset(
    std::string uuid,
    std::string name,
    std::string source_identity,
    std::uint64_t revision,
    UiScriptDescription description
) : uuid_(std::move(uuid)),
    name_(std::move(name)),
    source_identity_(std::move(source_identity)),
    revision_(revision),
    description_(std::move(description)) {}

std::string UiDocumentAsset::compiled_json(int indent) const {
    tc::trent payload = tc::trent::dict();
    payload.set(
        "ui_document_asset",
        static_cast<std::int64_t>(UI_DOCUMENT_ASSET_SCHEMA_VERSION));
    payload.set("uuid", uuid_);
    payload.set("name", name_);
    payload.set("source_identity", source_identity_);
    payload.set("revision", static_cast<std::int64_t>(revision_));
    tc::trent dependencies = tc::trent::list();
    for (const std::string& dependency : description_.type_dependencies) {
        dependencies.push_back(dependency);
    }
    payload.set("type_dependencies", std::move(dependencies));
    payload.set("recipe", encode_recipe(description_));
    return tc::json::dump(payload, indent);
}

bool TcUiDocumentAsset::valid() const {
    return handle_.valid();
}

std::shared_ptr<const UiDocumentAsset> TcUiDocumentAsset::resolve() const {
    std::lock_guard lock(registry_mutex());
    return resolve_locked(handle_);
}

std::string TcUiDocumentAsset::uuid() const {
    const auto asset = resolve();
    return asset ? asset->uuid() : std::string{};
}

std::string TcUiDocumentAsset::name() const {
    const auto asset = resolve();
    return asset ? asset->name() : std::string{};
}

std::uint64_t TcUiDocumentAsset::revision() const {
    const auto asset = resolve();
    return asset ? asset->revision() : 0;
}

tc_value TcUiDocumentAsset::serialize_to_value() const {
    tc_value result = tc_value_dict_new();
    const auto asset = resolve();
    if (!asset) {
        return result;
    }
    tc_value_dict_set(
        &result, "uuid", tc_value_string(asset->uuid().c_str()));
    tc_value_dict_set(
        &result, "name", tc_value_string(asset->name().c_str()));
    return result;
}

LoadedUiScript TcUiDocumentAsset::instantiate(TcDocument document) const {
    const auto asset = resolve();
    if (!asset) {
        throw UiScriptError(
            "cannot instantiate an invalid native UI document asset handle");
    }
    try {
        return UiScriptLoader().materialize(asset->description(), document);
    } catch (const std::exception& error) {
        throw UiScriptError(
            (asset->source_identity().empty()
                ? std::string("<memory>")
                : asset->source_identity()) +
            ": " + error.what());
    }
}

LoadedUiScript TcUiDocumentAsset::reload_instance(
    LoadedUiScript& loaded
) const {
    const auto asset = resolve();
    if (!asset) {
        throw UiScriptError(
            "cannot reload from an invalid native UI document asset handle");
    }
    try {
        return UiScriptLoader().reload(loaded, asset->description());
    } catch (const std::exception& error) {
        throw UiScriptError(
            (asset->source_identity().empty()
                ? std::string("<memory>")
                : asset->source_identity()) +
            ": " + error.what());
    }
}

bool TcUiDocumentAsset::reload_source(const std::string& source) {
    const auto current = resolve();
    if (!current) {
        tc_log_error(
            "[gui-native-ui-asset] cannot reload an invalid asset handle");
        return false;
    }

    std::shared_ptr<UiDocumentAsset> replacement;
    try {
        replacement = build_source_asset(
            current->uuid(), current->name(), current->source_identity(),
            source, current->revision() + 1);
    } catch (const std::exception& error) {
        tc_log_error(
            "[gui-native-ui-asset] failed to reload '%s' from '%s': %s",
            current->uuid().c_str(),
            current->source_identity().c_str(),
            error.what());
        return false;
    }

    std::lock_guard lock(registry_mutex());
    if (!resolve_locked(handle_)) {
        tc_log_error(
            "[gui-native-ui-asset] asset '%s' disappeared during reload",
            current->uuid().c_str());
        return false;
    }
    registry_slots()[handle_.index].asset = std::move(replacement);
    return true;
}

bool TcUiDocumentAsset::remove() {
    std::lock_guard lock(registry_mutex());
    const auto asset = resolve_locked(handle_);
    if (!asset) {
        return false;
    }
    registry_uuids().erase(asset->uuid());
    AssetSlot& slot = registry_slots()[handle_.index];
    slot.asset.reset();
    ++slot.generation;
    if (slot.generation == 0) {
        slot.generation = 1;
    }
    handle_ = {};
    return true;
}

TcUiDocumentAsset TcUiDocumentAsset::declare_source(
    std::string uuid,
    std::string name,
    std::string source_identity,
    const std::string& source
) {
    const std::string diagnostic_source = source_identity;
    try {
        return register_asset(build_source_asset(
            std::move(uuid), std::move(name), std::move(source_identity),
            source, 1));
    } catch (const std::exception& error) {
        tc_log_error(
            "[gui-native-ui-asset] failed to declare source asset '%s': %s",
            diagnostic_source.c_str(), error.what());
        return {};
    }
}

TcUiDocumentAsset TcUiDocumentAsset::declare_compiled_json(
    const std::string& compiled_json,
    const std::string& expected_uuid
) {
    try {
        return register_asset(
            build_compiled_asset(compiled_json, expected_uuid));
    } catch (const std::exception& error) {
        tc_log_error(
            "[gui-native-ui-asset] failed to declare compiled asset: %s",
            error.what());
        return {};
    }
}

std::string TcUiDocumentAsset::compile_source_json(
    std::string uuid,
    std::string name,
    std::string source_identity,
    const std::string& source,
    int indent
) {
    return build_source_asset(
        std::move(uuid), std::move(name), std::move(source_identity),
        source, 1)->compiled_json(indent);
}

TcUiDocumentAsset TcUiDocumentAsset::from_uuid(
    const std::string& uuid
) {
    std::lock_guard lock(registry_mutex());
    const auto found = registry_uuids().find(uuid);
    if (found == registry_uuids().end()) {
        return {};
    }
    const AssetSlot& slot = registry_slots()[found->second];
    return TcUiDocumentAsset(
        UiDocumentAssetHandle{found->second, slot.generation});
}

void TcUiDocumentAsset::clear_registry_for_tests() {
    std::lock_guard lock(registry_mutex());
    registry_uuids().clear();
    for (AssetSlot& slot : registry_slots()) {
        slot.asset.reset();
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
    }
}

} // namespace termin::gui_native
