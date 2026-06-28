#include <render/tc_pass.h>
#include <render/tc_pipeline.h>
#include <inspect/tc_runtime_type_registry.h>
#include <tc_pipeline_registry.h>
#include <tc_type_registry.h>
#include <tcbase/tc_log.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define tc_strdup _strdup
#else
#define tc_strdup strdup
#endif

static tc_type_registry* g_pass_registry = NULL;

#define TC_RUNTIME_TYPE_FACET_FRAME_PASS "termin.render.frame_pass"

static void ensure_pass_registry_initialized(void) {
    if (!g_pass_registry) {
        g_pass_registry = tc_type_registry_new();
    }
}

static void destroy_pass_facet(void* payload) {
    tc_type_entry* entry = (tc_type_entry*)payload;
    if (!entry || !entry->type_name || !g_pass_registry) {
        return;
    }
    tc_type_registry_unregister(g_pass_registry, entry->type_name);
}

bool tc_pass_link_registered_type(tc_pass* p, const char* type_name) {
    if (!p || !type_name) {
        return false;
    }
    if (!g_pass_registry) {
        tc_log(
            TC_LOG_ERROR,
            "[tc_pass] cannot link pass instance to type '%s': registry is not initialized",
            type_name
        );
        return false;
    }

    tc_type_entry* entry = tc_type_registry_get(g_pass_registry, type_name);
    if (!entry || !entry->registered) {
        tc_log(
            TC_LOG_ERROR,
            "[tc_pass] cannot link pass instance to unregistered type '%s'",
            type_name
        );
        return false;
    }

    if (!tc_runtime_type_registry_link_instance(
            type_name,
            &p->runtime_type_link,
            p
        )) {
        tc_log(
            TC_LOG_ERROR,
            "[tc_pass] failed to link pass instance to runtime type '%s'",
            type_name
        );
        return false;
    }

    p->type_entry = entry;
    p->type_version = entry->version;
    return true;
}

void tc_pass_set_name(tc_pass* p, const char* name) {
    if (!p) return;
    if (p->pass_name) free(p->pass_name);
    p->pass_name = name ? tc_strdup(name) : NULL;
}

void tc_pass_set_enabled(tc_pass* p, bool enabled) {
    if (p) p->enabled = enabled;
}

void tc_pass_set_passthrough(tc_pass* p, bool passthrough) {
    if (p) p->passthrough = passthrough;
}

void tc_pass_registry_register(
    const char* type_name,
    tc_pass_factory factory,
    void* factory_userdata,
    tc_pass_kind kind
) {
    if (!type_name) return;
    ensure_pass_registry_initialized();
    tc_type_entry* entry = tc_type_registry_register(
        g_pass_registry,
        type_name,
        (tc_type_factory_fn)factory,
        factory_userdata,
        (int)kind
    );
    if (entry) {
        tc_runtime_type_registry_ensure_type(type_name);
        tc_runtime_type_registry_set_facet(
            type_name,
            TC_RUNTIME_TYPE_FACET_FRAME_PASS,
            entry,
            destroy_pass_facet,
            1
        );
    }
}

void tc_pass_registry_unregister(const char* type_name) {
    if (!type_name || !g_pass_registry) return;
    tc_runtime_type_registry_remove_facet(type_name, TC_RUNTIME_TYPE_FACET_FRAME_PASS);
    tc_type_registry_unregister(g_pass_registry, type_name);
}

bool tc_pass_registry_has(const char* type_name) {
    if (!g_pass_registry) return false;
    return tc_type_registry_has(g_pass_registry, type_name);
}

tc_pass* tc_pass_registry_create(const char* type_name) {
    if (!g_pass_registry) return NULL;

    tc_type_entry* entry = tc_type_registry_get(g_pass_registry, type_name);
    if (!entry || !entry->registered || !entry->factory) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Unknown type or no factory: %s", type_name);
        return NULL;
    }

    tc_pass* p = (tc_pass*)tc_type_entry_create(entry);
    if (p) {
        p->kind = (tc_pass_kind)entry->kind;
        tc_pass_link_registered_type(p, type_name);
    }
    return p;
}

size_t tc_pass_registry_type_count(void) {
    if (!g_pass_registry) return 0;
    return tc_type_registry_count(g_pass_registry);
}

const char* tc_pass_registry_type_at(size_t index) {
    if (!g_pass_registry) return NULL;
    return tc_type_registry_type_at(g_pass_registry, index);
}

tc_pass_kind tc_pass_registry_get_kind(const char* type_name) {
    if (!g_pass_registry) return TC_NATIVE_PASS;
    tc_type_entry* entry = tc_type_registry_get(g_pass_registry, type_name);
    return entry ? (tc_pass_kind)entry->kind : TC_NATIVE_PASS;
}

tc_type_entry* tc_pass_registry_get_entry(const char* type_name) {
    if (!type_name || !g_pass_registry) return NULL;
    return tc_type_registry_get(g_pass_registry, type_name);
}

size_t tc_pass_registry_instance_count(const char* type_name) {
    if (!type_name) return 0;
    return tc_runtime_type_registry_instance_count(type_name);
}

void tc_pass_unlink_from_registry(tc_pass* p) {
    if (!p) return;
    tc_runtime_type_registry_unlink_instance(&p->runtime_type_link);
    p->type_entry = NULL;
    p->type_version = 0;
}

void tc_pass_registry_cleanup(void) {
    if (g_pass_registry) {
        tc_type_registry_free(g_pass_registry);
        g_pass_registry = NULL;
    }
}

static tc_external_pass_callbacks g_external_callbacks = {0};

static void external_execute(tc_pass* p, void* ctx) {
    if (g_external_callbacks.execute && p->body) {
        g_external_callbacks.execute(p->body, ctx);
    }
}

static size_t external_get_reads(tc_pass* p, const char** out, size_t max) {
    if (g_external_callbacks.get_reads && p->body) {
        return g_external_callbacks.get_reads(p->body, out, max);
    }
    return 0;
}

static size_t external_get_writes(tc_pass* p, const char** out, size_t max) {
    if (g_external_callbacks.get_writes && p->body) {
        return g_external_callbacks.get_writes(p->body, out, max);
    }
    return 0;
}

static size_t external_get_inplace_aliases(tc_pass* p, const char** out, size_t max) {
    if (g_external_callbacks.get_inplace_aliases && p->body) {
        return g_external_callbacks.get_inplace_aliases(p->body, out, max);
    }
    return 0;
}

static size_t external_get_resource_specs(tc_pass* p, void* out, size_t max) {
    if (g_external_callbacks.get_resource_specs && p->body) {
        return g_external_callbacks.get_resource_specs(p->body, out, max);
    }
    return 0;
}

static size_t external_get_internal_symbols(tc_pass* p, const char** out, size_t max) {
    if (g_external_callbacks.get_internal_symbols && p->body) {
        return g_external_callbacks.get_internal_symbols(p->body, out, max);
    }
    return 0;
}

static void external_destroy(tc_pass* p) {
    if (g_external_callbacks.destroy && p->body) {
        g_external_callbacks.destroy(p->body);
    }
}

static const tc_pass_vtable g_external_vtable = {
    .execute = external_execute,
    .get_reads = external_get_reads,
    .get_writes = external_get_writes,
    .get_inplace_aliases = external_get_inplace_aliases,
    .get_resource_specs = external_get_resource_specs,
    .get_internal_symbols = external_get_internal_symbols,
    .destroy = external_destroy,
    .serialize = NULL,
    .deserialize = NULL,
};

void tc_pass_set_external_callbacks(const tc_external_pass_callbacks* callbacks) {
    if (callbacks) {
        g_external_callbacks = *callbacks;
    }
}

tc_pass* tc_pass_new_external(void* body, const char* type_name, const tc_pass_ref_vtable* ref_vtable) {
    tc_pass* p = (tc_pass*)calloc(1, sizeof(tc_pass));
    if (!p) return NULL;

    tc_pass_init(p, &g_external_vtable);
    p->body = body;
    p->ref_vtable = ref_vtable;
    p->kind = TC_EXTERNAL_PASS;

    if (!type_name) {
        tc_log(TC_LOG_ERROR, "[tc_pass_new_external] type_name is NULL!");
        free(p);
        return NULL;
    }

    ensure_pass_registry_initialized();
    tc_type_entry* entry = tc_type_registry_get(g_pass_registry, type_name);
    if (!entry) {
        tc_log(TC_LOG_ERROR, "[tc_pass_new_external] type '%s' not registered! Call tc_pass_registry_register() first.", type_name);
        free(p);
        return NULL;
    }

    tc_pass_link_registered_type(p, type_name);

    return p;
}

void tc_pass_free_external(tc_pass* p) {
    if (!p) return;
    tc_pass_unlink_from_registry(p);
    if (p->pass_name) free(p->pass_name);
    if (p->viewport_name) free(p->viewport_name);
    if (p->debug_internal_symbol) free(p->debug_internal_symbol);
    free(p);
}
