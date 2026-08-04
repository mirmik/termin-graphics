#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <termin/materials/surface_contract_registry.h>

namespace termin {

struct SurfaceContractKey {
    std::string id;
    uint32_t version = 0;

    bool operator==(const SurfaceContractKey&) const = default;
};

struct SurfaceContractDescriptor {
    SurfaceContractKey key;
    std::string debug_name;
    std::string surface_type_name;
    std::string interface_source;
    std::string source_identity;

    bool operator==(const SurfaceContractDescriptor&) const = default;
};

class TERMIN_MATERIALS_API SurfaceContractRegistry {
public:
    static bool register_contract(
        const SurfaceContractDescriptor& descriptor,
        std::string_view owner
    );
    static std::optional<SurfaceContractDescriptor> find(
        const SurfaceContractKey& key
    );
    static std::optional<std::string> owner_of(const SurfaceContractKey& key);
    static bool unregister_contract(
        const SurfaceContractKey& key,
        std::string_view owner
    );
    static size_t unregister_owner(std::string_view owner);
    static bool register_builtins();
};

} // namespace termin
