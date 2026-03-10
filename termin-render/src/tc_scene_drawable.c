#include "core/tc_scene_drawable.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_entity_pool_registry.h"

typedef struct drawable_callback_context {
    tc_component_iter_fn callback;
    void* user_data;
    uint64_t layer_mask;
} drawable_callback_context;

static bool drawable_callback_adapter(tc_component* c, void* user_data) {
    drawable_callback_context* ctx = (drawable_callback_context*)user_data;
    bool check_layer = ctx->layer_mask != 0;
    if (check_layer && tc_entity_handle_valid(c->owner)) {
        tc_entity_pool* pool = tc_entity_pool_registry_get(c->owner.pool);
        if (pool) {
            uint64_t entity_layer = tc_entity_pool_layer(pool, c->owner.id);
            if (!(ctx->layer_mask & (UINT64_C(1) << entity_layer))) {
                return true;
            }
        }
    }
    return ctx->callback(c, ctx->user_data);
}

void tc_scene_foreach_drawable(
    tc_scene_handle h,
    tc_component_iter_fn callback,
    void* user_data,
    int filter_flags,
    uint64_t layer_mask
) {
    drawable_callback_context ctx;

    if (!callback) return;

    ctx.callback = callback;
    ctx.user_data = user_data;
    ctx.layer_mask = layer_mask;

    tc_component_cap_id drawable_cap = tc_drawable_capability_id();
    if (drawable_cap == TC_COMPONENT_CAPABILITY_INVALID_ID) return;

    tc_scene_foreach_with_capability(h, drawable_cap, drawable_callback_adapter, &ctx, filter_flags);
}
