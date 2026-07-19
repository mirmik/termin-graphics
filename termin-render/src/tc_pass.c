#include <render/tc_pass.h>
#include <render/tc_pipeline.h>
#include <inspect/tc_runtime_type_registry.h>
#include <tc_pipeline_registry.h>
#include <tcbase/tc_log.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define tc_strdup _strdup
#else
#define tc_strdup strdup
#endif

static tc_pass_prepare_unload_fn g_prepare_unload_callback = NULL;
static void* g_prepare_unload_user_data = NULL;

#define TC_RUNTIME_TYPE_FACET_FRAME_PASS "termin.render.frame_pass"

typedef struct tc_frame_pass_facet_payload {
    tc_pass_factory factory;
    void* factory_userdata;
    tc_pass_kind kind;
} tc_frame_pass_facet_payload;

static tc_frame_pass_facet_payload* pass_facet(const char* type_name) {
    return (tc_frame_pass_facet_payload*)tc_runtime_type_registry_get_facet(
        type_name,
        TC_RUNTIME_TYPE_FACET_FRAME_PASS
    );
}

static void destroy_pass_facet(void* payload) {
    free(payload);
}

static bool prepare_pass_facet_unload(
    const char* type_name,
    void* payload,
    void* context
) {
    (void)payload;
    const size_t instance_count = tc_runtime_type_registry_instance_count(type_name);
    if (!type_name || instance_count == 0) {
        return true;
    }
    if (!g_prepare_unload_callback) {
        tc_log(
            TC_LOG_ERROR,
            "[tc_pass] refusing to unload pass type '%s' with %zu live instance(s): "
            "no prepare-unload callback is installed",
            type_name,
            instance_count
        );
        return false;
    }
    return g_prepare_unload_callback(
        type_name,
        context,
        g_prepare_unload_user_data
    );
}

bool tc_pass_type_descriptor_add_facet(
    tc_runtime_type_descriptor* descriptor,
    tc_pass_factory factory,
    void* factory_userdata,
    tc_pass_kind kind
) {
    if (!descriptor) {
        tc_log(TC_LOG_ERROR, "[tc_pass] cannot attach a pass facet to a null descriptor");
        return false;
    }
    tc_frame_pass_facet_payload* facet =
        (tc_frame_pass_facet_payload*)calloc(1, sizeof(*facet));
    if (!facet) {
        tc_log(TC_LOG_ERROR, "[tc_pass] failed to allocate staged pass facet");
        return false;
    }
    facet->factory = factory;
    facet->factory_userdata = factory_userdata;
    facet->kind = kind;
    if (!tc_runtime_type_descriptor_add_facet(
            descriptor,
            TC_RUNTIME_TYPE_FACET_FRAME_PASS,
            facet,
            destroy_pass_facet,
            prepare_pass_facet_unload,
            1)) {
        return false;
    }
    return true;
}

typedef struct pass_type_collect_ctx {
    const char** names;
    size_t count;
    size_t capacity;
} pass_type_collect_ctx;

static bool collect_pass_type(const char* type_name, void* user_data) {
    pass_type_collect_ctx* ctx = (pass_type_collect_ctx*)user_data;
    if (!type_name || !ctx) {
        return true;
    }

    if (ctx->count >= ctx->capacity) {
        size_t new_capacity = ctx->capacity == 0 ? 8 : ctx->capacity * 2;
        const char** names = (const char**)realloc(
            ctx->names,
            new_capacity * sizeof(const char*)
        );
        if (!names) {
            tc_log(TC_LOG_ERROR, "[tc_pass] failed to grow pass type list during registry cleanup");
            return false;
        }
        ctx->names = names;
        ctx->capacity = new_capacity;
    }

    ctx->names[ctx->count++] = type_name;
    return true;
}

bool tc_pass_link_registered_type(tc_pass* p, const char* type_name) {
    if (!p || !type_name) {
        return false;
    }
    if (!pass_facet(type_name)) {
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

void tc_pass_set_viewport_name(tc_pass* p, const char* viewport_name) {
    if (!p) return;
    free(p->viewport_name);
    p->viewport_name = viewport_name && viewport_name[0]
        ? tc_strdup(viewport_name)
        : NULL;
}

void tc_pass_delete_unowned(tc_pass* p) {
    if (!p) return;
    if (tc_pipeline_handle_valid(p->owner_pipeline)) {
        tc_log(
            TC_LOG_ERROR,
            "[tc_pass] refusing to delete pass '%s' while it belongs to a pipeline",
            p->pass_name ? p->pass_name : "<unnamed>"
        );
        return;
    }
    tc_pass_deleter deleter = p->deleter;
    if (!deleter) {
        tc_log(
            TC_LOG_ERROR,
            "[tc_pass] cannot delete unowned pass '%s': no deleter is installed",
            p->pass_name ? p->pass_name : "<unnamed>"
        );
        return;
    }
    p->deleter = NULL;
    tc_pass_destroy(p);
    deleter(p);
}

void tc_pass_registry_register(
    const char* type_name,
    tc_pass_factory factory,
    void* factory_userdata,
    tc_pass_kind kind
) {
    if (!type_name) return;
    tc_runtime_type_registry_ensure_type(type_name);
    tc_frame_pass_facet_payload* facet = pass_facet(type_name);
    if (facet) {
        facet->factory = factory;
        facet->factory_userdata = factory_userdata;
        facet->kind = kind;
        return;
    }
    facet = (tc_frame_pass_facet_payload*)calloc(1, sizeof(*facet));
    if (facet) {
        facet->factory = factory;
        facet->factory_userdata = factory_userdata;
        facet->kind = kind;
        if (!tc_runtime_type_registry_set_facet_with_lifecycle(
            type_name,
            TC_RUNTIME_TYPE_FACET_FRAME_PASS,
            facet,
            destroy_pass_facet,
            prepare_pass_facet_unload,
            1
        )) {
            free(facet);
        }
    }
}

void tc_pass_registry_unregister(const char* type_name) {
    if (!type_name) return;
    if (!tc_runtime_type_registry_unregister_type_with_context(type_name, NULL)) {
        tc_log(TC_LOG_ERROR, "[tc_pass] failed to unregister pass type '%s'", type_name);
    }
}

bool tc_pass_registry_has(const char* type_name) {
    return pass_facet(type_name) != NULL;
}

tc_pass* tc_pass_registry_create(const char* type_name) {
    tc_frame_pass_facet_payload* facet = pass_facet(type_name);
    if (!facet || !facet->factory) {
        tc_log(TC_LOG_ERROR, "[tc_pass] Unknown type or no factory: %s", type_name);
        return NULL;
    }

    tc_pass* p = facet->factory(facet->factory_userdata);
    if (p) {
        p->kind = facet->kind;
        tc_pass_link_registered_type(p, type_name);
    }
    return p;
}

size_t tc_pass_registry_type_count(void) {
    return tc_runtime_type_registry_types_with_facet_count(TC_RUNTIME_TYPE_FACET_FRAME_PASS);
}

const char* tc_pass_registry_type_at(size_t index) {
    return tc_runtime_type_registry_type_with_facet_at(TC_RUNTIME_TYPE_FACET_FRAME_PASS, index);
}

tc_pass_kind tc_pass_registry_get_kind(const char* type_name) {
    tc_frame_pass_facet_payload* facet = pass_facet(type_name);
    return facet ? facet->kind : TC_NATIVE_PASS;
}

size_t tc_pass_registry_instance_count(const char* type_name) {
    if (!type_name) return 0;
    return tc_runtime_type_registry_instance_count(type_name);
}

void tc_pass_registry_set_prepare_unload_callback(
    tc_pass_prepare_unload_fn callback,
    void* user_data
) {
    g_prepare_unload_callback = callback;
    g_prepare_unload_user_data = user_data;
}

void tc_pass_unlink_from_registry(tc_pass* p) {
    if (!p) return;
    tc_runtime_type_registry_unlink_instance(&p->runtime_type_link);
}

void tc_pass_registry_cleanup(void) {
    g_prepare_unload_callback = NULL;
    g_prepare_unload_user_data = NULL;
    pass_type_collect_ctx ctx = { NULL, 0, 0 };
    tc_runtime_type_registry_foreach_type_with_facet(
        TC_RUNTIME_TYPE_FACET_FRAME_PASS,
        collect_pass_type,
        &ctx
    );
    for (size_t i = 0; i < ctx.count; ++i) {
        if (!tc_runtime_type_registry_unregister_type_with_context(ctx.names[i], NULL)) {
            tc_log(TC_LOG_ERROR, "[tc_pass] failed to clean up pass type '%s'", ctx.names[i]);
        }
    }
    free(ctx.names);

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

tc_pass* tc_pass_new_external(void* body, const char* type_name) {
    tc_pass* p = (tc_pass*)calloc(1, sizeof(tc_pass));
    if (!p) return NULL;

    tc_pass_init_unowned(p, &g_external_vtable);
    p->body = body;
    p->kind = TC_EXTERNAL_PASS;

    if (!type_name) {
        tc_log(TC_LOG_ERROR, "[tc_pass_new_external] type_name is NULL!");
        free(p);
        return NULL;
    }

    if (!pass_facet(type_name)) {
        tc_log(TC_LOG_ERROR,
               "[tc_pass_new_external] type '%s' is not registered; publish its frame-pass type descriptor first",
               type_name);
        free(p);
        return NULL;
    }

    tc_pass_link_registered_type(p, type_name);

    return p;
}

void tc_pass_free_external(tc_pass* p) {
    if (!p) return;
    if (tc_pipeline_handle_valid(p->owner_pipeline)) {
        tc_log(
            TC_LOG_ERROR,
            "[tc_pass] refusing to free external pass '%s' while it belongs to a pipeline",
            p->pass_name ? p->pass_name : "<unnamed>"
        );
        return;
    }
    tc_pass_unlink_from_registry(p);
    if (p->pass_name) free(p->pass_name);
    if (p->viewport_name) free(p->viewport_name);
    if (p->debug_internal_symbol) free(p->debug_internal_symbol);
    free(p);
}
