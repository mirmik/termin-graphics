#ifndef TC_PASS_H
#define TC_PASS_H

#include <tc_types.h>
#include <core/tc_entity_pool.h>
#include <core/tc_scene_pool.h>
#include <core/tc_dlist.h>
#include <inspect/tc_runtime_type_registry.h>
#include <render/tc_pipeline_pool.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_pass tc_pass;
typedef struct tc_pass_vtable tc_pass_vtable;
typedef struct tc_execute_context tc_execute_context;
typedef void (*tc_pass_deleter)(tc_pass* pass);

typedef enum tc_pass_kind {
    TC_NATIVE_PASS = 0,
    TC_EXTERNAL_PASS = 1
} tc_pass_kind;

struct tc_execute_context {
    void* graphics;
    void* reads_fbos;
    void* writes_fbos;
    int rect_x;
    int rect_y;
    int rect_width;
    int rect_height;
    tc_scene_handle scene;
    const char* viewport_name;
    tc_entity_handle internal_entities;
    void* camera;
    void* lights;
    size_t light_count;
    uint64_t layer_mask;
};

struct tc_pass_vtable {
    void (*execute)(tc_pass* self, void* ctx);
    // Dependency enumeration uses a count/fill contract. Implementations return
    // the complete item count even when out is NULL or max_count is smaller.
    size_t (*get_reads)(tc_pass* self, const char** out_reads, size_t max_count);
    size_t (*get_writes)(tc_pass* self, const char** out_writes, size_t max_count);
    size_t (*get_inplace_aliases)(tc_pass* self, const char** out_pairs, size_t max_pairs);
    size_t (*get_resource_specs)(tc_pass* self, void* out_specs, size_t max_count);
    size_t (*get_internal_symbols)(tc_pass* self, const char** out_symbols, size_t max_count);
    void (*destroy)(tc_pass* self);
    void* (*serialize)(const tc_pass* self);
    void (*deserialize)(tc_pass* self, const void* data);
};

struct tc_pass {
    const tc_pass_vtable* vtable;
    char* pass_name;
    bool enabled;
    bool passthrough;
    char* viewport_name;
    tc_pass_kind kind;
    tc_language native_language;
    void* body;
    void* bindings[TC_LANGUAGE_MAX];
    tc_pipeline_handle owner_pipeline;
    tc_pass_deleter deleter;
    tc_runtime_type_instance_link runtime_type_link;
};

static inline void tc_pass_init_unowned(tc_pass* p, const tc_pass_vtable* vtable) {
    p->vtable = vtable;
    p->pass_name = NULL;
    p->enabled = true;
    p->passthrough = false;
    p->viewport_name = NULL;
    p->kind = TC_NATIVE_PASS;
    p->native_language = TC_LANGUAGE_CXX;
    p->body = NULL;
    for (int i = 0; i < TC_LANGUAGE_MAX; i++) {
        p->bindings[i] = NULL;
    }
    p->owner_pipeline = TC_PIPELINE_HANDLE_INVALID;
    p->deleter = NULL;
    tc_runtime_type_instance_link_init(&p->runtime_type_link);
}

static inline void* tc_pass_get_binding(tc_pass* p, tc_language lang) {
    if (!p || lang < 0 || lang >= TC_LANGUAGE_MAX) return NULL;
    return p->bindings[lang];
}

static inline void tc_pass_set_binding(tc_pass* p, tc_language lang, void* binding) {
    if (!p || lang < 0 || lang >= TC_LANGUAGE_MAX) return;
    p->bindings[lang] = binding;
}

static inline void tc_pass_clear_binding(tc_pass* p, tc_language lang) {
    if (!p || lang < 0 || lang >= TC_LANGUAGE_MAX) return;
    p->bindings[lang] = NULL;
}

static inline void tc_pass_execute(tc_pass* p, void* ctx) {
    if (p && p->enabled && !p->passthrough && p->vtable && p->vtable->execute) {
        p->vtable->execute(p, ctx);
    }
}

static inline const char* tc_pass_type_name(const tc_pass* p) {
    if (p && p->runtime_type_link.type_name) {
        return p->runtime_type_link.type_name;
    }
    return "BrokenPass_NoRuntimeTypeLink";
}

static inline size_t tc_pass_get_reads(tc_pass* p, const char** out, size_t max) {
    if (p && p->vtable && p->vtable->get_reads) {
        return p->vtable->get_reads(p, out, max);
    }
    return 0;
}

static inline size_t tc_pass_get_writes(tc_pass* p, const char** out, size_t max) {
    if (p && p->vtable && p->vtable->get_writes) {
        return p->vtable->get_writes(p, out, max);
    }
    return 0;
}

static inline size_t tc_pass_get_inplace_aliases(tc_pass* p, const char** out, size_t max) {
    if (p && p->vtable && p->vtable->get_inplace_aliases) {
        return p->vtable->get_inplace_aliases(p, out, max);
    }
    return 0;
}

static inline bool tc_pass_is_inplace(tc_pass* p) {
    const char* dummy[2];
    return tc_pass_get_inplace_aliases(p, dummy, 1) > 0;
}

static inline size_t tc_pass_get_resource_specs(tc_pass* p, void* out, size_t max) {
    if (p && p->vtable && p->vtable->get_resource_specs) {
        return p->vtable->get_resource_specs(p, out, max);
    }
    return 0;
}

static inline size_t tc_pass_get_internal_symbols(tc_pass* p, const char** out, size_t max) {
    if (p && p->vtable && p->vtable->get_internal_symbols) {
        return p->vtable->get_internal_symbols(p, out, max);
    }
    return 0;
}

static inline void tc_pass_destroy(tc_pass* p) {
    if (p && p->vtable && p->vtable->destroy) {
        p->vtable->destroy(p);
    }
}

TC_API void tc_pass_delete_unowned(tc_pass* p);

TC_API void tc_pass_set_name(tc_pass* p, const char* name);
TC_API void tc_pass_set_enabled(tc_pass* p, bool enabled);
TC_API void tc_pass_set_passthrough(tc_pass* p, bool passthrough);
TC_API void tc_pass_set_viewport_name(tc_pass* p, const char* viewport_name);

typedef tc_pass* (*tc_pass_factory)(void* userdata);
typedef bool (*tc_pass_prepare_unload_fn)(
    const char* type_name,
    void* context,
    void* user_data
);

// Stage the frame-pass facet on a runtime type descriptor. The descriptor
// owns the facet payload after a successful call and publishes it only when
// the descriptor itself is committed.
TC_API bool tc_pass_type_descriptor_add_facet(
    tc_runtime_type_descriptor* descriptor,
    tc_pass_factory factory,
    void* factory_userdata,
    tc_pass_kind kind
);

TC_API void tc_pass_registry_unregister(const char* type_name);
TC_API bool tc_pass_registry_has(const char* type_name);
// Returns a new unowned pass. Transfer it to exactly one pipeline or call
// tc_pass_delete_unowned() on failure.
TC_API tc_pass* tc_pass_registry_create(const char* type_name);
TC_API void tc_pass_registry_cleanup(void);
TC_API size_t tc_pass_registry_type_count(void);
TC_API const char* tc_pass_registry_type_at(size_t index);
TC_API tc_pass_kind tc_pass_registry_get_kind(const char* type_name);
TC_API size_t tc_pass_registry_instance_count(const char* type_name);
TC_API void tc_pass_registry_set_prepare_unload_callback(
    tc_pass_prepare_unload_fn callback,
    void* user_data
);
TC_API bool tc_pass_link_registered_type(tc_pass* p, const char* type_name);
TC_API void tc_pass_unlink_from_registry(tc_pass* p);

static inline bool tc_pass_type_is_current(const tc_pass* p) {
    if (!p || !p->runtime_type_link.type_name) return true;
    return tc_runtime_type_registry_instance_is_current(&p->runtime_type_link);
}

typedef struct {
    void (*execute)(void* body, void* ctx);
    // Reads, writes and inplace aliases follow the same complete count/fill
    // contract as tc_pass_vtable.
    size_t (*get_reads)(void* body, const char** out, size_t max);
    size_t (*get_writes)(void* body, const char** out, size_t max);
    size_t (*get_inplace_aliases)(void* body, const char** out, size_t max);
    size_t (*get_resource_specs)(void* body, void* out, size_t max);
    size_t (*get_internal_symbols)(void* body, const char** out, size_t max);
    void (*destroy)(void* body);
} tc_external_pass_callbacks;

TC_API void tc_pass_set_external_callbacks(const tc_external_pass_callbacks* callbacks);
TC_API tc_pass* tc_pass_new_external(void* body, const char* type_name);
TC_API void tc_pass_free_external(tc_pass* p);

#ifdef __cplusplus
}
#endif

#endif
