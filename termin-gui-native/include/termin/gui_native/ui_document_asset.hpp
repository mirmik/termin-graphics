#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <tc_value.h>
#include <termin/gui_native/export.h>
#include <termin/gui_native/uiscript.hpp>

namespace termin::gui_native {

inline constexpr std::uint32_t UI_DOCUMENT_ASSET_SCHEMA_VERSION = 1;

struct UiDocumentAssetHandle {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0;

    bool valid() const;
    friend bool operator==(
        UiDocumentAssetHandle left,
        UiDocumentAssetHandle right
    ) = default;
};

class TERMIN_GUI_NATIVE_API UiDocumentAsset {
public:
    UiDocumentAsset(
        std::string uuid,
        std::string name,
        std::string source_identity,
        std::uint64_t revision,
        UiScriptDescription description
    );

    const std::string& uuid() const { return uuid_; }
    const std::string& name() const { return name_; }
    const std::string& source_identity() const { return source_identity_; }
    std::uint64_t revision() const { return revision_; }
    const UiScriptDescription& description() const { return description_; }
    const std::vector<std::string>& type_dependencies() const {
        return description_.type_dependencies;
    }
    std::string compiled_json(int indent = 2) const;

private:
    std::string uuid_;
    std::string name_;
    std::string source_identity_;
    std::uint64_t revision_ = 1;
    UiScriptDescription description_;
};

// Generation-safe reference into the process native UI asset registry.
class TERMIN_GUI_NATIVE_API TcUiDocumentAsset {
public:
    TcUiDocumentAsset() = default;
    explicit TcUiDocumentAsset(UiDocumentAssetHandle handle)
        : handle_(handle) {}

    UiDocumentAssetHandle handle() const { return handle_; }
    bool valid() const;
    bool is_valid() const { return valid(); }
    std::shared_ptr<const UiDocumentAsset> resolve() const;
    std::string uuid() const;
    std::string name() const;
    std::uint64_t revision() const;
    tc_value serialize_to_value() const;

    LoadedUiScript instantiate(TcDocument document = {}) const;
    LoadedUiScript reload_instance(LoadedUiScript& loaded) const;
    bool reload_source(const std::string& source);
    bool remove();

    static TcUiDocumentAsset declare_source(
        std::string uuid,
        std::string name,
        std::string source_identity,
        const std::string& source
    );
    static TcUiDocumentAsset declare_compiled_json(
        const std::string& compiled_json,
        const std::string& expected_uuid = {}
    );
    static std::string compile_source_json(
        std::string uuid,
        std::string name,
        std::string source_identity,
        const std::string& source,
        int indent = 2
    );
    static TcUiDocumentAsset from_uuid(const std::string& uuid);
    static void clear_registry_for_tests();

private:
    UiDocumentAssetHandle handle_;
};

} // namespace termin::gui_native
