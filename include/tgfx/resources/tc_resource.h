// tc_resource.h - Common resource header for mesh, texture, etc.
#pragma once

#include "tgfx/tgfx_api.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Generic load callback
// ============================================================================
// Called when resource data is needed but not loaded.
// The resource pointer is the containing struct (tc_mesh*, tc_texture*, etc.)
// Returns true if loading succeeded, false otherwise.

typedef bool (*tc_resource_load_fn)(void* resource, void* user_data);

// ============================================================================
// Resource header - common fields for all resources
// ============================================================================
// Place this as the FIRST field in resource structs for consistent layout.

#define TC_UUID_SIZE 40

typedef struct tc_resource_header {
    char uuid[TC_UUID_SIZE];        // unique identifier
    const char* name;               // human-readable name (interned string)
    uint32_t version;               // incremented on data change (for GPU sync)
    uint32_t ref_count;             // reference count for ownership
    uint32_t pool_index;            // index in resource pool (for GPUContext lookup)
    uint8_t is_loaded;              // true if data is loaded
    uint8_t _pad[3];
    tc_resource_load_fn load_callback;  // callback to load data (NULL if no lazy loading)
    void* load_user_data;               // user data for load callback
} tc_resource_header;

// ============================================================================
// Resource header helpers
// ============================================================================

// Initialize resource header with UUID
static inline void tc_resource_header_init(tc_resource_header* header, const char* uuid) {
    if (uuid && uuid[0] != '\0') {
        size_t len = strlen(uuid);
        if (len >= TC_UUID_SIZE) len = TC_UUID_SIZE - 1;
        memcpy(header->uuid, uuid, len);
        header->uuid[len] = '\0';
    } else {
        header->uuid[0] = '\0';
    }
    header->name = NULL;
    header->version = 1;
    header->ref_count = 0;
    header->pool_index = 0;
    header->is_loaded = 0;
    header->load_callback = NULL;
    header->load_user_data = NULL;
}

// Set load callback
static inline void tc_resource_header_set_load_callback(
    tc_resource_header* header,
    tc_resource_load_fn callback,
    void* user_data
) {
    header->load_callback = callback;
    header->load_user_data = user_data;
}

// Trigger load if not loaded and callback exists
// Returns true if loaded (or was already loaded), false if no callback or load failed
static inline bool tc_resource_header_ensure_loaded(tc_resource_header* header, void* resource) {
    if (header->is_loaded) return true;
    if (!header->load_callback) return false;

    bool success = header->load_callback(resource, header->load_user_data);
    if (success) {
        header->is_loaded = 1;
    }
    return success;
}

// Bump version (call after data change)
static inline void tc_resource_header_bump_version(tc_resource_header* header) {
    header->version++;
}

#ifdef __cplusplus
}
#endif
