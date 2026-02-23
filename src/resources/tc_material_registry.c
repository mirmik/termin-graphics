// tc_material_registry.c - Material registry with pool + hash table
#include "tgfx/resources/tc_material_registry.h"
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/tc_pool.h"
#include "tgfx/tc_resource_map.h"
#include "tgfx/tc_registry_utils.h"
#include <tcbase/tc_log.h>
#include "tgfx/tgfx_intern_string.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Global state
// ============================================================================

static tc_pool g_material_pool;
static tc_resource_map* g_uuid_to_index = NULL;
static uint64_t g_next_uuid = 1;
static bool g_initialized = false;

// ============================================================================
// Lifecycle
// ============================================================================

void tc_material_init(void) {
    TC_REGISTRY_INIT_GUARD(g_initialized, "tc_material");

    if (!tc_pool_init(&g_material_pool, sizeof(tc_material), 64)) {
        tc_log(TC_LOG_ERROR, "tc_material_init: failed to init pool");
        return;
    }

    g_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_uuid_to_index) {
        tc_log(TC_LOG_ERROR, "tc_material_init: failed to create uuid map");
        tc_pool_free(&g_material_pool);
        return;
    }

    g_next_uuid = 1;
    g_initialized = true;
}

void tc_material_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_initialized, "tc_material");

    // Release shader references for all materials before freeing pool
    for (uint32_t i = 0; i < g_material_pool.capacity; i++) {
        if (g_material_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_material* mat = (tc_material*)tc_pool_get_unchecked(&g_material_pool, i);
            for (size_t j = 0; j < mat->phase_count; j++) {
                tc_shader* s = tc_shader_get(mat->phases[j].shader);
                if (s) {
                    tc_shader_release(s);
                }
            }
        }
    }

    tc_pool_free(&g_material_pool);
    tc_resource_map_free(g_uuid_to_index);
    g_uuid_to_index = NULL;
    g_next_uuid = 1;
    g_initialized = false;
}

// ============================================================================
// Handle-based API
// ============================================================================

tc_material_handle tc_material_create(const char* uuid, const char* name) {
    if (!g_initialized) {
        tc_material_init();
    }

    if (!name || name[0] == '\0') {
        tc_log(TC_LOG_ERROR, "tc_material_create: name is required");
        return tc_material_handle_invalid();
    }

    char uuid_buf[TC_UUID_SIZE];
    const char* final_uuid;

    if (uuid && uuid[0] != '\0') {
        if (tc_material_contains(uuid)) {
            tc_log(TC_LOG_WARN, "tc_material_create: uuid '%s' already exists", uuid);
            return tc_material_handle_invalid();
        }
        final_uuid = uuid;
    } else {
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "mat", &g_next_uuid);
        final_uuid = uuid_buf;
    }

    tc_handle h = tc_pool_alloc(&g_material_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log(TC_LOG_ERROR, "tc_material_create: pool alloc failed");
        return tc_material_handle_invalid();
    }

    tc_material* mat = (tc_material*)tc_pool_get(&g_material_pool, h);
    memset(mat, 0, sizeof(tc_material));
    strncpy(mat->header.uuid, final_uuid, sizeof(mat->header.uuid) - 1);
    mat->header.uuid[sizeof(mat->header.uuid) - 1] = '\0';
    mat->header.name = tgfx_intern_string(name);
    mat->header.version = 1;
    mat->header.ref_count = 0;
    mat->header.is_loaded = 1;

    if (!tc_resource_map_add(g_uuid_to_index, mat->header.uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_material_create: failed to add to uuid map");
        tc_pool_free_slot(&g_material_pool, h);
        return tc_material_handle_invalid();
    }

    return h;
}

tc_material_handle tc_material_find(const char* uuid) {
    if (!g_initialized || !uuid) {
        return tc_material_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_uuid_to_index, uuid);
    if (!tc_has_index(ptr)) {
        return tc_material_handle_invalid();
    }

    uint32_t index = tc_unpack_index(ptr);
    if (index >= g_material_pool.capacity) {
        return tc_material_handle_invalid();
    }

    if (g_material_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_material_handle_invalid();
    }

    tc_material_handle h;
    h.index = index;
    h.generation = g_material_pool.generations[index];
    return h;
}

tc_material_handle tc_material_find_by_name(const char* name) {
    if (!g_initialized || !name) {
        return tc_material_handle_invalid();
    }

    for (uint32_t i = 0; i < g_material_pool.capacity; i++) {
        if (g_material_pool.states[i] == TC_SLOT_OCCUPIED) {
            tc_material* mat = (tc_material*)tc_pool_get_unchecked(&g_material_pool, i);
            if (mat->header.name && strcmp(mat->header.name, name) == 0) {
                tc_material_handle h;
                h.index = i;
                h.generation = g_material_pool.generations[i];
                return h;
            }
        }
    }

    return tc_material_handle_invalid();
}

tc_material_handle tc_material_get_or_create(const char* uuid, const char* name) {
    if (!uuid || uuid[0] == '\0') {
        tc_log(TC_LOG_WARN, "tc_material_get_or_create: empty uuid");
        return tc_material_handle_invalid();
    }

    tc_material_handle h = tc_material_find(uuid);
    if (!tc_material_handle_is_invalid(h)) {
        return h;
    }

    return tc_material_create(uuid, name);
}

tc_material* tc_material_get(tc_material_handle h) {
    if (!g_initialized || tc_material_handle_is_invalid(h)) {
        return NULL;
    }

    if (!tc_pool_is_valid(&g_material_pool, h)) {
        return NULL;
    }

    return (tc_material*)tc_pool_get(&g_material_pool, h);
}

bool tc_material_is_valid(tc_material_handle h) {
    if (!g_initialized || tc_material_handle_is_invalid(h)) {
        return false;
    }
    return tc_pool_is_valid(&g_material_pool, h);
}

// Helper to release all shader references in a material
static void material_release_shaders(tc_material* mat) {
    if (!mat) return;
    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_shader* s = tc_shader_get(mat->phases[i].shader);
        if (s) {
            tc_shader_release(s);
        }
    }
}

bool tc_material_destroy(tc_material_handle h) {
    if (!g_initialized) return false;

    tc_material* mat = tc_material_get(h);
    if (!mat) return false;

    // Release all shader references before destroying
    material_release_shaders(mat);

    tc_resource_map_remove(g_uuid_to_index, mat->header.uuid);
    tc_pool_free_slot(&g_material_pool, h);

    return true;
}

bool tc_material_contains(const char* uuid) {
    return !tc_material_handle_is_invalid(tc_material_find(uuid));
}

size_t tc_material_count(void) {
    if (!g_initialized) return 0;
    return tc_pool_count(&g_material_pool);
}

// ============================================================================
// Reference counting
// ============================================================================

void tc_material_add_ref(tc_material* mat) {
    if (mat) {
        mat->header.ref_count++;
    }
}

bool tc_material_release(tc_material* mat) {
    if (!mat || mat->header.ref_count == 0) return false;

    mat->header.ref_count--;
    if (mat->header.ref_count == 0) {
        tc_material_handle h = tc_material_find(mat->header.uuid);
        if (!tc_material_handle_is_invalid(h)) {
            tc_material_destroy(h);
        }
        return true;
    }
    return false;
}

// ============================================================================
// Phase operations
// ============================================================================

tc_material_phase* tc_material_add_phase(
    tc_material* mat,
    tc_shader_handle shader,
    const char* phase_mark,
    int priority
) {
    if (!mat || mat->phase_count >= TC_MATERIAL_MAX_PHASES) {
        return NULL;
    }

    // Add reference to shader (material owns it now)
    tc_shader* s = tc_shader_get(shader);
    if (s) {
        tc_shader_add_ref(s);
    }

    tc_material_phase* phase = &mat->phases[mat->phase_count];
    memset(phase, 0, sizeof(tc_material_phase));

    phase->shader = shader;
    phase->state = tc_render_state_opaque();
    phase->priority = priority;

    if (phase_mark) {
        strncpy(phase->phase_mark, phase_mark, TC_PHASE_MARK_MAX - 1);
    } else {
        strcpy(phase->phase_mark, "opaque");
    }

    mat->phase_count++;
    mat->header.version++;

    return phase;
}

bool tc_material_remove_phase(tc_material* mat, size_t index) {
    if (!mat || index >= mat->phase_count) {
        return false;
    }

    // Release shader reference for removed phase
    tc_shader* s = tc_shader_get(mat->phases[index].shader);
    if (s) {
        tc_shader_release(s);
    }

    for (size_t i = index; i < mat->phase_count - 1; i++) {
        mat->phases[i] = mat->phases[i + 1];
    }

    mat->phase_count--;
    mat->header.version++;

    return true;
}

size_t tc_material_get_phases_for_mark(
    tc_material* mat,
    const char* mark,
    tc_material_phase** out_phases,
    size_t max_count
) {
    if (!mat || !mark || !out_phases || max_count == 0) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < mat->phase_count && count < max_count; i++) {
        if (strcmp(mat->phases[i].phase_mark, mark) == 0) {
            out_phases[count++] = &mat->phases[i];
        }
    }

    // Sort by priority (simple bubble sort for small arrays)
    for (size_t i = 0; i + 1 < count; i++) {
        for (size_t j = 0; j + 1 < count - i; j++) {
            if (out_phases[j]->priority > out_phases[j + 1]->priority) {
                tc_material_phase* tmp = out_phases[j];
                out_phases[j] = out_phases[j + 1];
                out_phases[j + 1] = tmp;
            }
        }
    }

    return count;
}

// ============================================================================
// Phase uniform/texture operations
// ============================================================================

bool tc_material_phase_set_uniform(
    tc_material_phase* phase,
    const char* name,
    tc_uniform_type type,
    const void* value
) {
    if (!phase || !name || !value) return false;

    tc_uniform_value* uniform = tc_material_phase_find_uniform(phase, name);
    if (!uniform) {
        if (phase->uniform_count >= TC_MATERIAL_MAX_UNIFORMS) {
            return false;
        }
        uniform = &phase->uniforms[phase->uniform_count++];
        strncpy(uniform->name, name, TC_UNIFORM_NAME_MAX - 1);
    }

    uniform->type = (uint8_t)type;

    switch (type) {
        case TC_UNIFORM_BOOL:
            uniform->data.i = *(const int*)value ? 1 : 0;
            break;
        case TC_UNIFORM_INT:
            uniform->data.i = *(const int*)value;
            break;
        case TC_UNIFORM_FLOAT:
            uniform->data.f = *(const float*)value;
            break;
        case TC_UNIFORM_VEC2:
            memcpy(uniform->data.v2, value, sizeof(float) * 2);
            break;
        case TC_UNIFORM_VEC3:
            memcpy(uniform->data.v3, value, sizeof(float) * 3);
            break;
        case TC_UNIFORM_VEC4:
            memcpy(uniform->data.v4, value, sizeof(float) * 4);
            break;
        case TC_UNIFORM_MAT4:
            memcpy(uniform->data.m4, value, sizeof(float) * 16);
            break;
        default:
            return false;
    }

    return true;
}

bool tc_material_phase_set_texture(
    tc_material_phase* phase,
    const char* name,
    tc_texture_handle texture
) {
    if (!phase || !name) return false;

    tc_material_texture* tex = tc_material_phase_find_texture(phase, name);
    if (!tex) {
        if (phase->texture_count >= TC_MATERIAL_MAX_TEXTURES) {
            return false;
        }
        tex = &phase->textures[phase->texture_count++];
        strncpy(tex->name, name, TC_UNIFORM_NAME_MAX - 1);
    }

    tex->texture = texture;
    return true;
}

bool tc_material_phase_get_color(
    const tc_material_phase* phase,
    float* r, float* g, float* b, float* a
) {
    if (!phase) return false;

    for (size_t i = 0; i < phase->uniform_count; i++) {
        if (strcmp(phase->uniforms[i].name, "u_color") == 0 &&
            phase->uniforms[i].type == TC_UNIFORM_VEC4) {
            if (r) *r = phase->uniforms[i].data.v4[0];
            if (g) *g = phase->uniforms[i].data.v4[1];
            if (b) *b = phase->uniforms[i].data.v4[2];
            if (a) *a = phase->uniforms[i].data.v4[3];
            return true;
        }
    }
    return false;
}

void tc_material_phase_set_color(
    tc_material_phase* phase,
    float r, float g, float b, float a
) {
    if (!phase) return;

    float color[4] = {r, g, b, a};
    tc_material_phase_set_uniform(phase, "u_color", TC_UNIFORM_VEC4, color);
}

void tc_material_phase_make_transparent(tc_material_phase* phase) {
    if (!phase) return;
    phase->state = tc_render_state_transparent();
}

// ============================================================================
// Material uniform/texture operations
// ============================================================================

tc_material_phase* tc_material_find_phase(tc_material* mat, const char* mark) {
    if (!mat || !mark) return NULL;

    for (size_t i = 0; i < mat->phase_count; i++) {
        if (strcmp(mat->phases[i].phase_mark, mark) == 0) {
            return &mat->phases[i];
        }
    }
    return NULL;
}

void tc_material_set_uniform(
    tc_material* mat,
    const char* name,
    tc_uniform_type type,
    const void* value
) {
    if (!mat) return;

    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_material_phase_set_uniform(&mat->phases[i], name, type, value);
    }
    // Note: uniforms are per-frame values, don't bump version
}

void tc_material_set_texture(
    tc_material* mat,
    const char* name,
    tc_texture_handle texture
) {
    if (!mat || !name) return;

    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_material_phase_set_texture(&mat->phases[i], name, texture);
    }

    tc_material_texture* th = NULL;
    for (size_t i = 0; i < mat->texture_handle_count; i++) {
        if (strcmp(mat->texture_handles[i].name, name) == 0) {
            th = &mat->texture_handles[i];
            break;
        }
    }
    if (!th && mat->texture_handle_count < TC_MATERIAL_MAX_TEXTURES) {
        th = &mat->texture_handles[mat->texture_handle_count++];
        strncpy(th->name, name, TC_UNIFORM_NAME_MAX - 1);
    }
    if (th) {
        th->texture = texture;
    }

    mat->header.version++;
}

bool tc_material_get_color(
    const tc_material* mat,
    float* r, float* g, float* b, float* a
) {
    if (!mat || mat->phase_count == 0) return false;
    return tc_material_phase_get_color(&mat->phases[0], r, g, b, a);
}

void tc_material_set_color(
    tc_material* mat,
    float r, float g, float b, float a
) {
    if (!mat) return;

    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_material_phase_set_color(&mat->phases[i], r, g, b, a);
    }
    mat->header.version++;
}

// ============================================================================
// Info collection
// ============================================================================

tc_material_info* tc_material_get_all_info(size_t* count) {
    if (!g_initialized || !count) {
        if (count) *count = 0;
        return NULL;
    }

    size_t alive = tc_pool_count(&g_material_pool);
    if (alive == 0) {
        *count = 0;
        return NULL;
    }

    tc_material_info* infos = (tc_material_info*)malloc(alive * sizeof(tc_material_info));
    if (!infos) {
        *count = 0;
        return NULL;
    }

    size_t idx = 0;
    for (uint32_t i = 0; i < g_material_pool.capacity && idx < alive; i++) {
        if (g_material_pool.states[i] != TC_SLOT_OCCUPIED) continue;

        tc_material* mat = (tc_material*)tc_pool_get_unchecked(&g_material_pool, i);
        if (!mat) continue;

        infos[idx].handle.index = i;
        infos[idx].handle.generation = g_material_pool.generations[i];
        strncpy(infos[idx].uuid, mat->header.uuid, sizeof(infos[idx].uuid));
        infos[idx].name = mat->header.name;
        infos[idx].ref_count = mat->header.ref_count;
        infos[idx].version = mat->header.version;
        infos[idx].phase_count = mat->phase_count;
        infos[idx].texture_count = mat->texture_handle_count;
        idx++;
    }

    *count = idx;
    return infos;
}

// ============================================================================
// Iteration
// ============================================================================

void tc_material_foreach(tc_material_iter_fn callback, void* user_data) {
    if (!g_initialized || !callback) return;

    for (uint32_t i = 0; i < g_material_pool.capacity; i++) {
        if (g_material_pool.states[i] != TC_SLOT_OCCUPIED) continue;

        tc_material* mat = (tc_material*)tc_pool_get_unchecked(&g_material_pool, i);
        if (!mat) continue;

        tc_material_handle h;
        h.index = i;
        h.generation = g_material_pool.generations[i];

        if (!callback(h, mat, user_data)) {
            break;
        }
    }
}

// ============================================================================
// Copy
// ============================================================================

tc_material_handle tc_material_copy(tc_material_handle src, const char* new_uuid) {
    tc_material* src_mat = tc_material_get(src);
    if (!src_mat) {
        return tc_material_handle_invalid();
    }

    if (!src_mat->header.name) {
        tc_log(TC_LOG_ERROR, "[tc_material_copy] source material '%s' has no name",
            src_mat->header.uuid);
        return tc_material_handle_invalid();
    }

    // Generate name for copy
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "%s_copy", src_mat->header.name);

    tc_material_handle dst = tc_material_create(new_uuid, name_buf);
    tc_material* dst_mat = tc_material_get(dst);
    if (!dst_mat) {
        return tc_material_handle_invalid();
    }

    // Copy phases and add shader references
    dst_mat->phase_count = src_mat->phase_count;
    for (size_t i = 0; i < src_mat->phase_count; i++) {
        dst_mat->phases[i] = src_mat->phases[i];
        // Add reference to shader for the copied phase
        tc_shader* s = tc_shader_get(dst_mat->phases[i].shader);
        if (s) {
            tc_shader_add_ref(s);
        }
    }

    // Copy texture handles
    dst_mat->texture_handle_count = src_mat->texture_handle_count;
    for (size_t i = 0; i < src_mat->texture_handle_count; i++) {
        dst_mat->texture_handles[i] = src_mat->texture_handles[i];
    }

    // Copy metadata
    strncpy(dst_mat->shader_name, src_mat->shader_name, TC_MATERIAL_NAME_MAX - 1);
    strncpy(dst_mat->active_phase_mark, src_mat->active_phase_mark, TC_PHASE_MARK_MAX - 1);

    return dst;
}
