#include <termin/materials/surface_contract_registry.h>
#include <termin/materials/surface_contract_registry.hpp>

#include <inspect/tc_runtime_type_registry.h>
#include <tcbase/tc_log.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct SurfaceContractRecord {
    std::string id;
    std::string debug_name;
    std::string surface_type_name;
    std::string interface_source;
    std::string source_identity;
    tc_surface_contract_desc view{};

    explicit SurfaceContractRecord(const tc_surface_contract_desc& source)
        : id(source.key.id),
          debug_name(source.debug_name),
          surface_type_name(source.surface_type_name),
          interface_source(source.interface_source),
          source_identity(source.source_identity)
    {
        view.abi_version = TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION;
        view.key.id = id.c_str();
        view.key.version = source.key.version;
        view.debug_name = debug_name.c_str();
        view.surface_type_name = surface_type_name.c_str();
        view.interface_source = interface_source.c_str();
        view.source_identity = source_identity.c_str();
    }
};

void destroy_surface_contract_record(void* payload) {
    delete static_cast<SurfaceContractRecord*>(payload);
}

bool valid_text(const char* value) {
    return value && value[0];
}

bool valid_key(tc_surface_contract_key key) {
    return valid_text(key.id) && key.version != 0;
}

bool valid_descriptor(const char* owner, const tc_surface_contract_desc* descriptor) {
    if (!valid_text(owner) || !descriptor || !valid_key(descriptor->key) ||
        !valid_text(descriptor->debug_name) ||
        !valid_text(descriptor->surface_type_name) ||
        !valid_text(descriptor->interface_source) ||
        !valid_text(descriptor->source_identity)) {
        tc_log_error(
            "[SurfaceContractRegistry] registration requires an explicit owner, "
            "non-zero exact version and non-empty descriptor strings"
        );
        return false;
    }
    if (descriptor->abi_version != TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION) {
        tc_log_error(
            "[SurfaceContractRegistry] contract '%s@%u' has unsupported descriptor ABI %u",
            descriptor->key.id,
            descriptor->key.version,
            descriptor->abi_version
        );
        return false;
    }
    return true;
}

std::string runtime_type_name(tc_surface_contract_key key) {
    return "termin.material.surface_contract/" +
        std::to_string(key.version) + "/" + key.id;
}

SurfaceContractRecord* find_record(tc_surface_contract_key key) {
    if (!valid_key(key)) {
        return nullptr;
    }
    const std::string type_name = runtime_type_name(key);
    return static_cast<SurfaceContractRecord*>(
        tc_runtime_type_registry_get_facet(
            type_name.c_str(),
            TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT
        )
    );
}

bool descriptors_equal(
    const SurfaceContractRecord& existing,
    const tc_surface_contract_desc& incoming
) {
    return existing.view.key.version == incoming.key.version &&
        existing.id == incoming.key.id &&
        existing.debug_name == incoming.debug_name &&
        existing.surface_type_name == incoming.surface_type_name &&
        existing.interface_source == incoming.interface_source &&
        existing.source_identity == incoming.source_identity;
}

template<typename Callback>
size_t collect_surface_contract_type_names(Callback&& include, std::vector<std::string>& out) {
    const size_t count = tc_runtime_type_registry_types_with_facet_count(
        TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT
    );
    out.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const char* type_name = tc_runtime_type_registry_type_with_facet_at(
            TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT,
            index
        );
        if (type_name && include(type_name)) {
            out.emplace_back(type_name);
        }
    }
    return out.size();
}

constexpr const char* STANDARD_PBR_INTERFACE_SOURCE = R"slang(
struct TerminStandardSurfaceV1 {
    float3 base_color;
    float3 normal_world;
    float metallic;
    float perceptual_roughness;
    float occlusion;
    float3 emission;
    float opacity;
};
)slang";

std::string standard_pbr_source_identity() {
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char ch : std::string_view(STANDARD_PBR_INTERFACE_SOURCE)) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    char buffer[96] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "termin.surface.standard-pbr@1:fnv1a64:%016llx",
        static_cast<unsigned long long>(hash)
    );
    return buffer;
}

termin::SurfaceContractDescriptor cpp_descriptor(
    const tc_surface_contract_desc& descriptor
) {
    return {
        {descriptor.key.id, descriptor.key.version},
        descriptor.debug_name,
        descriptor.surface_type_name,
        descriptor.interface_source,
        descriptor.source_identity,
    };
}

} // namespace

extern "C" {

bool tc_surface_contract_registry_register(
    const char* owner,
    const tc_surface_contract_desc* descriptor
) {
    if (!valid_descriptor(owner, descriptor)) {
        return false;
    }
    try {
        const std::string type_name = runtime_type_name(descriptor->key);
        if (SurfaceContractRecord* existing = find_record(descriptor->key)) {
            const char* existing_owner =
                tc_runtime_type_registry_get_owner(type_name.c_str());
            if (existing_owner && std::strcmp(existing_owner, owner) == 0 &&
                descriptors_equal(*existing, *descriptor)) {
                tc_log_info(
                    "[SurfaceContractRegistry] duplicate identical registration "
                    "is idempotent: '%s@%u' owner='%s'",
                    descriptor->key.id,
                    descriptor->key.version,
                    owner
                );
                return true;
            }
            tc_log_error(
                "[SurfaceContractRegistry] conflicting registration rejected: "
                "'%s@%u' existing_owner='%s' incoming_owner='%s'",
                descriptor->key.id,
                descriptor->key.version,
                existing_owner ? existing_owner : "<unknown>",
                owner
            );
            return false;
        }

        std::unique_ptr<SurfaceContractRecord> record(
            new (std::nothrow) SurfaceContractRecord(*descriptor)
        );
        if (!record) {
            tc_log_error(
                "[SurfaceContractRegistry] failed to allocate contract '%s@%u'",
                descriptor->key.id,
                descriptor->key.version
            );
            return false;
        }
        tc_runtime_type_descriptor* type_descriptor =
            tc_runtime_type_descriptor_create(type_name.c_str(), owner, nullptr);
        if (!type_descriptor) {
            return false;
        }
        if (!tc_runtime_type_descriptor_add_facet(
                type_descriptor,
                TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT,
                record.release(),
                destroy_surface_contract_record,
                nullptr,
                TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION)) {
            tc_runtime_type_descriptor_destroy(type_descriptor);
            return false;
        }
        if (!tc_runtime_type_registry_commit_descriptor(type_descriptor)) {
            tc_log_error(
                "[SurfaceContractRegistry] failed to publish contract '%s@%u' owner='%s'",
                descriptor->key.id,
                descriptor->key.version,
                owner
            );
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        tc_log_error(
            "[SurfaceContractRegistry] registration failed for '%s@%u': %s",
            descriptor->key.id,
            descriptor->key.version,
            error.what()
        );
        return false;
    } catch (...) {
        tc_log_error(
            "[SurfaceContractRegistry] registration failed for '%s@%u'",
            descriptor->key.id,
            descriptor->key.version
        );
        return false;
    }
}

const tc_surface_contract_desc*
tc_surface_contract_registry_find(tc_surface_contract_key key) {
    try {
        SurfaceContractRecord* record = find_record(key);
        return record ? &record->view : nullptr;
    } catch (const std::exception& error) {
        tc_log_error(
            "[SurfaceContractRegistry] lookup failed for '%s@%u': %s",
            key.id ? key.id : "<null>",
            key.version,
            error.what()
        );
        return nullptr;
    }
}

const char* tc_surface_contract_registry_owner(tc_surface_contract_key key) {
    if (!valid_key(key)) {
        return nullptr;
    }
    try {
        const std::string type_name = runtime_type_name(key);
        if (!tc_runtime_type_registry_has_facet(
                type_name.c_str(),
                TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT)) {
            return nullptr;
        }
        return tc_runtime_type_registry_get_owner(type_name.c_str());
    } catch (const std::exception& error) {
        tc_log_error(
            "[SurfaceContractRegistry] owner lookup failed for '%s@%u': %s",
            key.id,
            key.version,
            error.what()
        );
        return nullptr;
    }
}

bool tc_surface_contract_registry_unregister(
    const char* owner,
    tc_surface_contract_key key
) {
    if (!valid_text(owner) || !valid_key(key)) {
        tc_log_error(
            "[SurfaceContractRegistry] unregister requires owner, contract id and non-zero version"
        );
        return false;
    }
    try {
        const std::string type_name = runtime_type_name(key);
        if (!tc_runtime_type_registry_has_facet(
                type_name.c_str(),
                TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT)) {
            tc_log_error(
                "[SurfaceContractRegistry] cannot unregister missing contract '%s@%u' owner='%s'",
                key.id,
                key.version,
                owner
            );
            return false;
        }
        const char* existing_owner =
            tc_runtime_type_registry_get_owner(type_name.c_str());
        if (!existing_owner || std::strcmp(existing_owner, owner) != 0) {
            tc_log_error(
                "[SurfaceContractRegistry] owner mismatch while unregistering '%s@%u': "
                "existing_owner='%s' requested_owner='%s'",
                key.id,
                key.version,
                existing_owner ? existing_owner : "<unknown>",
                owner
            );
            return false;
        }
        if (!tc_runtime_type_registry_unregister_type_with_context(
                type_name.c_str(),
                nullptr)) {
            tc_log_error(
                "[SurfaceContractRegistry] runtime registry refused unload for '%s@%u'",
                key.id,
                key.version
            );
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        tc_log_error(
            "[SurfaceContractRegistry] unregister failed for '%s@%u': %s",
            key.id,
            key.version,
            error.what()
        );
        return false;
    }
}

size_t tc_surface_contract_registry_unregister_owner(const char* owner) {
    if (!valid_text(owner)) {
        tc_log_error("[SurfaceContractRegistry] cannot unregister an unnamed owner");
        return 0;
    }
    try {
        std::vector<std::string> names;
        collect_surface_contract_type_names(
            [owner](const char* type_name) {
                const char* registered_owner =
                    tc_runtime_type_registry_get_owner(type_name);
                return registered_owner &&
                    std::strcmp(registered_owner, owner) == 0;
            },
            names
        );
        size_t removed = 0;
        for (const std::string& type_name : names) {
            if (tc_runtime_type_registry_unregister_type_with_context(
                    type_name.c_str(),
                    nullptr)) {
                ++removed;
            } else {
                tc_log_error(
                    "[SurfaceContractRegistry] runtime registry refused owner unload "
                    "for type '%s' owner='%s'",
                    type_name.c_str(),
                    owner
                );
            }
        }
        return removed;
    } catch (const std::exception& error) {
        tc_log_error(
            "[SurfaceContractRegistry] owner unload failed for '%s': %s",
            owner,
            error.what()
        );
        return 0;
    }
}

size_t tc_surface_contract_registry_count(void) {
    return tc_runtime_type_registry_types_with_facet_count(
        TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT
    );
}

const tc_surface_contract_desc*
tc_surface_contract_registry_at(size_t index) {
    const char* type_name = tc_runtime_type_registry_type_with_facet_at(
        TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT,
        index
    );
    SurfaceContractRecord* record = type_name
        ? static_cast<SurfaceContractRecord*>(
              tc_runtime_type_registry_get_facet(
                  type_name,
                  TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT))
        : nullptr;
    return record ? &record->view : nullptr;
}

void tc_surface_contract_registry_clear(void) {
    try {
        std::vector<std::string> names;
        collect_surface_contract_type_names(
            [](const char*) { return true; },
            names
        );
        for (const std::string& type_name : names) {
            if (!tc_runtime_type_registry_unregister_type_with_context(
                    type_name.c_str(),
                    nullptr)) {
                tc_log_error(
                    "[SurfaceContractRegistry] failed to clear type '%s'",
                    type_name.c_str()
                );
            }
        }
    } catch (const std::exception& error) {
        tc_log_error(
            "[SurfaceContractRegistry] clear failed: %s",
            error.what()
        );
    }
}

bool tc_surface_contract_registry_register_builtins(void) {
    try {
        const std::string source_identity = standard_pbr_source_identity();
        const tc_surface_contract_desc descriptor = {
            TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION,
            {
                TC_STANDARD_PBR_SURFACE_CONTRACT_ID,
                TC_STANDARD_PBR_SURFACE_CONTRACT_VERSION,
            },
            "Termin Standard PBR Surface v1",
            "TerminStandardSurfaceV1",
            STANDARD_PBR_INTERFACE_SOURCE,
            source_identity.c_str(),
        };
        return tc_surface_contract_registry_register(
            "termin-materials",
            &descriptor
        );
    } catch (const std::exception& error) {
        tc_log_error(
            "[SurfaceContractRegistry] built-in registration failed: %s",
            error.what()
        );
        return false;
    }
}

} // extern "C"

namespace termin {

bool SurfaceContractRegistry::register_contract(
    const SurfaceContractDescriptor& descriptor,
    std::string_view owner
) {
    const std::string owner_storage(owner);
    const tc_surface_contract_desc projection = {
        TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION,
        {descriptor.key.id.c_str(), descriptor.key.version},
        descriptor.debug_name.c_str(),
        descriptor.surface_type_name.c_str(),
        descriptor.interface_source.c_str(),
        descriptor.source_identity.c_str(),
    };
    return tc_surface_contract_registry_register(
        owner_storage.c_str(),
        &projection
    );
}

std::optional<SurfaceContractDescriptor> SurfaceContractRegistry::find(
    const SurfaceContractKey& key
) {
    const tc_surface_contract_desc* descriptor =
        tc_surface_contract_registry_find({key.id.c_str(), key.version});
    if (!descriptor) {
        return std::nullopt;
    }
    return cpp_descriptor(*descriptor);
}

std::optional<std::string> SurfaceContractRegistry::owner_of(
    const SurfaceContractKey& key
) {
    const char* owner =
        tc_surface_contract_registry_owner({key.id.c_str(), key.version});
    return owner ? std::optional<std::string>(owner) : std::nullopt;
}

bool SurfaceContractRegistry::unregister_contract(
    const SurfaceContractKey& key,
    std::string_view owner
) {
    const std::string owner_storage(owner);
    return tc_surface_contract_registry_unregister(
        owner_storage.c_str(),
        {key.id.c_str(), key.version}
    );
}

size_t SurfaceContractRegistry::unregister_owner(std::string_view owner) {
    const std::string owner_storage(owner);
    return tc_surface_contract_registry_unregister_owner(
        owner_storage.c_str()
    );
}

bool SurfaceContractRegistry::register_builtins() {
    return tc_surface_contract_registry_register_builtins();
}

} // namespace termin
