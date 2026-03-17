// tc_scene_pipeline_template.c - Scene pipeline template registry

#include "core/tc_scene_pipeline_template.h"
#include <tcbase/tc_log.h>
#include <tcbase/tgfx_intern_string.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define tc_strdup _strdup
#else
#define tc_strdup strdup
#endif

// ============================================================================
// Pool configuration
// ============================================================================

#define TC_SPT_POOL_INITIAL_CAPACITY 32

typedef struct tc_spt_slot {
    tc_scene_pipeline_template data;
    uint32_t generation;
    bool alive;
} tc_spt_slot;

static tc_spt_slot* g_spt_pool = NULL;
static size_t g_spt_pool_capacity = 0;
static size_t g_spt_pool_count = 0;

// ============================================================================
// Pool management
// ============================================================================

static void tc_spt_pool_ensure_capacity(void) {
    if (g_spt_pool == NULL) {
        g_spt_pool_capacity = TC_SPT_POOL_INITIAL_CAPACITY;
        g_spt_pool = (tc_spt_slot*)calloc(g_spt_pool_capacity, sizeof(tc_spt_slot));
        return;
    }

    if (g_spt_pool_count >= g_spt_pool_capacity) {
        size_t new_capacity = g_spt_pool_capacity * 2;
        tc_spt_slot* new_pool = (tc_spt_slot*)realloc(g_spt_pool, new_capacity * sizeof(tc_spt_slot));
        if (new_pool) {
            memset(new_pool + g_spt_pool_capacity, 0,
                   (new_capacity - g_spt_pool_capacity) * sizeof(tc_spt_slot));
            g_spt_pool = new_pool;
            g_spt_pool_capacity = new_capacity;
        }
    }
}

static tc_spt_slot* tc_spt_get_slot(tc_spt_handle h) {
    if (h.index >= g_spt_pool_capacity) return NULL;
    tc_spt_slot* slot = &g_spt_pool[h.index];
    if (!slot->alive || slot->generation != h.generation) return NULL;
    return slot;
}

// ============================================================================
// Extract target viewports from graph_data
// ============================================================================

static void tc_spt_extract_viewports(tc_scene_pipeline_template* tpl) {
    // Free existing
    if (tpl->target_viewports) {
        for (size_t i = 0; i < tpl->target_viewport_count; i++) {
            free(tpl->target_viewports[i]);
        }
        free(tpl->target_viewports);
        tpl->target_viewports = NULL;
        tpl->target_viewport_count = 0;
    }

    if (tpl->graph_data.type != TC_VALUE_DICT) return;

    // Look for viewport_frames in graph_data
    tc_value* viewport_frames = tc_value_dict_get(&tpl->graph_data, "viewport_frames");
    if (!viewport_frames || viewport_frames->type != TC_VALUE_LIST) return;

    size_t count = tc_value_list_size(viewport_frames);
    if (count == 0) return;

    tpl->target_viewports = (char**)calloc(count, sizeof(char*));
    tpl->target_viewport_count = 0;

    for (size_t i = 0; i < count; i++) {
        tc_value* frame = tc_value_list_get(viewport_frames, i);
        if (!frame || frame->type != TC_VALUE_DICT) continue;

        tc_value* vp_name = tc_value_dict_get(frame, "viewport_name");
        if (vp_name && vp_name->type == TC_VALUE_STRING && vp_name->data.s) {
            // Check for duplicates
            bool duplicate = false;
            for (size_t j = 0; j < tpl->target_viewport_count; j++) {
                if (strcmp(tpl->target_viewports[j], vp_name->data.s) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                tpl->target_viewports[tpl->target_viewport_count] = tc_strdup(vp_name->data.s);
                tpl->target_viewport_count++;
            }
        }
    }
}

// ============================================================================
// Registry API
// ============================================================================

tc_spt_handle tc_spt_declare(const char* uuid, const char* name) {
    tc_spt_pool_ensure_capacity();

    // Find free slot or reuse
    uint32_t index = 0;
    bool found_free = false;

    // First check if already exists by UUID
    if (uuid && uuid[0]) {
        for (size_t i = 0; i < g_spt_pool_count; i++) {
            if (g_spt_pool[i].alive) {
                const char* existing_uuid = g_spt_pool[i].data.header.uuid;
                if (existing_uuid[0] && strcmp(existing_uuid, uuid) == 0) {
                    // Already exists, return existing handle
                    tc_spt_handle h = { (uint32_t)i, g_spt_pool[i].generation };
                    return h;
                }
            }
        }
    }

    // Find free slot
    for (size_t i = 0; i < g_spt_pool_count; i++) {
        if (!g_spt_pool[i].alive) {
            index = (uint32_t)i;
            found_free = true;
            break;
        }
    }

    if (!found_free) {
        if (g_spt_pool_count >= g_spt_pool_capacity) {
            tc_spt_pool_ensure_capacity();
        }
        index = (uint32_t)g_spt_pool_count;
        g_spt_pool_count++;
    }

    tc_spt_slot* slot = &g_spt_pool[index];
    slot->generation++;
    slot->alive = true;

    // Initialize template
    memset(&slot->data, 0, sizeof(tc_scene_pipeline_template));
    tc_resource_header_init(&slot->data.header, uuid);
    slot->data.header.name = name ? tgfx_intern_string(name) : NULL;
    slot->data.graph_data = tc_value_nil();

    tc_spt_handle h = { index, slot->generation };
    return h;
}

tc_scene_pipeline_template* tc_spt_get(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    return slot ? &slot->data : NULL;
}

bool tc_spt_is_valid(tc_spt_handle h) {
    return tc_spt_get_slot(h) != NULL;
}

bool tc_spt_is_loaded(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return false;
    return slot->data.header.is_loaded != 0;
}

tc_spt_handle tc_spt_find_by_uuid(const char* uuid) {
    if (!uuid || !uuid[0]) return TC_SPT_HANDLE_INVALID;

    for (size_t i = 0; i < g_spt_pool_count; i++) {
        if (g_spt_pool[i].alive) {
            const char* existing = g_spt_pool[i].data.header.uuid;
            if (existing[0] && strcmp(existing, uuid) == 0) {
                tc_spt_handle h = { (uint32_t)i, g_spt_pool[i].generation };
                return h;
            }
        }
    }

    return TC_SPT_HANDLE_INVALID;
}

tc_spt_handle tc_spt_find_by_name(const char* name) {
    if (!name || !name[0]) return TC_SPT_HANDLE_INVALID;

    for (size_t i = 0; i < g_spt_pool_count; i++) {
        if (g_spt_pool[i].alive) {
            const char* existing = g_spt_pool[i].data.header.name;
            if (existing && strcmp(existing, name) == 0) {
                tc_spt_handle h = { (uint32_t)i, g_spt_pool[i].generation };
                return h;
            }
        }
    }

    return TC_SPT_HANDLE_INVALID;
}

// ============================================================================
// Graph data
// ============================================================================

void tc_spt_set_graph(tc_spt_handle h, tc_value graph) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) {
        tc_value_free(&graph);
        return;
    }

    // Free existing
    tc_value_free(&slot->data.graph_data);

    // Take ownership
    slot->data.graph_data = graph;
    slot->data.header.is_loaded = 1;
    tc_resource_header_bump_version(&slot->data.header);

    // Extract target viewports
    tc_spt_extract_viewports(&slot->data);
}

const tc_value* tc_spt_get_graph(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return NULL;

    // Trigger lazy load if needed
    if (!slot->data.header.is_loaded) {
        tc_spt_ensure_loaded(h);
    }

    return &slot->data.graph_data;
}

// ============================================================================
// Accessors
// ============================================================================

const char* tc_spt_get_uuid(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return "";
    return slot->data.header.uuid;
}

const char* tc_spt_get_name(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return "";
    return slot->data.header.name ? slot->data.header.name : "";
}

void tc_spt_set_name(tc_spt_handle h, const char* name) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return;
    slot->data.header.name = name ? tgfx_intern_string(name) : NULL;
}

// ============================================================================
// Target viewports
// ============================================================================

size_t tc_spt_viewport_count(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return 0;
    return slot->data.target_viewport_count;
}

const char* tc_spt_get_viewport(tc_spt_handle h, size_t index) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot || index >= slot->data.target_viewport_count) return "";
    return slot->data.target_viewports[index];
}

// ============================================================================
// Lazy loading
// ============================================================================

void tc_spt_set_load_callback(
    tc_spt_handle h,
    tc_resource_load_fn callback,
    void* user_data
) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return;
    tc_resource_header_set_load_callback(&slot->data.header, callback, user_data);
}

bool tc_spt_ensure_loaded(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return false;
    return tc_resource_header_ensure_loaded(&slot->data.header, &slot->data);
}

// ============================================================================
// Lifecycle
// ============================================================================

void tc_spt_free(tc_spt_handle h) {
    tc_spt_slot* slot = tc_spt_get_slot(h);
    if (!slot) return;

    // Free graph data
    tc_value_free(&slot->data.graph_data);

    // Free target viewports
    if (slot->data.target_viewports) {
        for (size_t i = 0; i < slot->data.target_viewport_count; i++) {
            free(slot->data.target_viewports[i]);
        }
        free(slot->data.target_viewports);
    }

    // Mark as dead (don't reset generation - it's used for handle validation)
    slot->alive = false;
    memset(&slot->data, 0, sizeof(tc_scene_pipeline_template));
}

void tc_spt_free_all(void) {
    if (g_spt_pool) {
        for (size_t i = 0; i < g_spt_pool_count; i++) {
            if (g_spt_pool[i].alive) {
                tc_spt_handle h = { (uint32_t)i, g_spt_pool[i].generation };
                tc_spt_free(h);
            }
        }
        free(g_spt_pool);
        g_spt_pool = NULL;
        g_spt_pool_capacity = 0;
        g_spt_pool_count = 0;
    }
}
