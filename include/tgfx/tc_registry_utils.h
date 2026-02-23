// tc_registry_utils.h - Utility macros for resource registries
#ifndef TC_REGISTRY_UTILS_H
#define TC_REGISTRY_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "tgfx/tgfx_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Index packing for resource maps
// ============================================================================
// Resource maps store void*, but we need uint32_t indices.
// We pack index+1 into void* so that NULL means "not found".

static inline void* tc_pack_index(uint32_t index) {
    return (void*)(uintptr_t)(index + 1);
}

static inline uint32_t tc_unpack_index(void* ptr) {
    return (uint32_t)((uintptr_t)ptr - 1);
}

static inline bool tc_has_index(void* ptr) {
    return ptr != NULL;
}

// ============================================================================
// UUID generation with prefix
// ============================================================================
// Generates UUID with given prefix: "prefix-0000000000000001"

static inline void tc_generate_prefixed_uuid(char* out_uuid, size_t out_size,
                                              const char* prefix, uint64_t* counter) {
    snprintf(out_uuid, out_size, "%s-%016llx", prefix, (unsigned long long)(*counter)++);
}

// ============================================================================
// Init/Shutdown guard macros
// ============================================================================

#define TC_REGISTRY_INIT_GUARD(initialized, name) \
    do { \
        if (initialized) { \
            tgfx_log(TGFX_LOG_WARN, name "_init: already initialized"); \
            return; \
        } \
    } while(0)

#define TC_REGISTRY_SHUTDOWN_GUARD(initialized, name) \
    do { \
        if (!initialized) { \
            tgfx_log(TGFX_LOG_WARN, name "_shutdown: not initialized"); \
            return; \
        } \
    } while(0)

#define TC_REGISTRY_CHECK_INIT(initialized, name, retval) \
    do { \
        if (!initialized) { \
            name##_init(); \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif // TC_REGISTRY_UTILS_H
