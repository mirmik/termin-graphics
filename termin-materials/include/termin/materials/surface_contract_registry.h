#ifndef TERMIN_MATERIALS_SURFACE_CONTRACT_REGISTRY_H
#define TERMIN_MATERIALS_SURFACE_CONTRACT_REGISTRY_H

#include <termin/materials/termin_materials_api.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION 1u
#define TC_RUNTIME_TYPE_FACET_SURFACE_CONTRACT "termin.material.surface_contract"

#define TC_STANDARD_PBR_SURFACE_CONTRACT_ID "termin.surface.standard-pbr"
#define TC_STANDARD_PBR_SURFACE_CONTRACT_VERSION 1u

typedef struct tc_surface_contract_key {
    const char* id;
    uint32_t version;
} tc_surface_contract_key;

/*
 * C-compatible registration boundary. All strings are borrowed only for the
 * duration of register(); the registry copies them before publication.
 */
typedef struct tc_surface_contract_desc {
    uint32_t abi_version;
    tc_surface_contract_key key;
    const char* debug_name;
    const char* surface_type_name;
    const char* interface_source;
    const char* source_identity;
} tc_surface_contract_desc;

/*
 * Register one exact (id, version) descriptor for an explicit owner.
 * Re-registering the same descriptor for the same owner is idempotent.
 */
TERMIN_MATERIALS_API bool tc_surface_contract_registry_register(
    const char* owner,
    const tc_surface_contract_desc* descriptor
);

/*
 * Returned descriptors point into registry-owned copied storage and remain
 * valid until that key or its owner is unregistered.
 */
TERMIN_MATERIALS_API const tc_surface_contract_desc*
tc_surface_contract_registry_find(tc_surface_contract_key key);
TERMIN_MATERIALS_API const char*
tc_surface_contract_registry_owner(tc_surface_contract_key key);

/*
 * Unregister requires the current owner. Missing keys and owner mismatches are
 * invalid unload uses and return false after logging an error.
 */
TERMIN_MATERIALS_API bool tc_surface_contract_registry_unregister(
    const char* owner,
    tc_surface_contract_key key
);
TERMIN_MATERIALS_API size_t
tc_surface_contract_registry_unregister_owner(const char* owner);

TERMIN_MATERIALS_API size_t tc_surface_contract_registry_count(void);
TERMIN_MATERIALS_API const tc_surface_contract_desc*
tc_surface_contract_registry_at(size_t index);
TERMIN_MATERIALS_API void tc_surface_contract_registry_clear(void);

/* Publishes built-ins through tc_surface_contract_registry_register(). */
TERMIN_MATERIALS_API bool tc_surface_contract_registry_register_builtins(void);

#ifdef __cplusplus
}
#endif

#endif /* TERMIN_MATERIALS_SURFACE_CONTRACT_REGISTRY_H */
