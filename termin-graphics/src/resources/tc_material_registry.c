// tc_material_registry.c - Material registry with pool + hash table
#include "tgfx/resources/tc_material_registry.h"
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/resources/tc_texture_registry.h"
#include <stdlib.h>
#include <string.h>
#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>
#include <tcbase/tc_registry_utils.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tc_string.h>

// ============================================================================
// Global state
// ============================================================================

static tc_pool g_material_pool;
static tc_pool_generation_epoch g_material_generation_epoch;
static tc_resource_map* g_material_uuid_to_index = NULL;
static uint64_t g_material_next_uuid = 1;
static bool g_material_initialized = false;

// ============================================================================
// Lifecycle
// ============================================================================

void tc_material_init(void) {
    TC_REGISTRY_INIT_GUARD(g_material_initialized, "tc_material");

    if (!tc_pool_init_rebootstrap(&g_material_pool, sizeof(tc_material), 64, &g_material_generation_epoch)) {
        tc_log(TC_LOG_ERROR, "tc_material_init: failed to init pool");
        return;
    }

    g_material_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_material_uuid_to_index) {
        tc_log(TC_LOG_ERROR, "tc_material_init: failed to create uuid map");
        tc_pool_free(&g_material_pool);
        return;
    }

    g_material_next_uuid = 1;
    g_material_initialized = true;
}

void tc_material_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_material_initialized, "tc_material");

    // Release shader references for all materials before freeing the pool.
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
    tc_resource_map_free(g_material_uuid_to_index);
    g_material_uuid_to_index = NULL;
    g_material_next_uuid = 1;
    g_material_initialized = false;
}

// ============================================================================
// Handle-based API
// ============================================================================

tc_material_handle tc_material_create(const char* uuid, const char* name) {
    if (!g_material_initialized) {
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
        tc_generate_prefixed_uuid(uuid_buf, sizeof(uuid_buf), "mat", &g_material_next_uuid);
        final_uuid = uuid_buf;
    }

    tc_handle h = tc_pool_alloc(&g_material_pool);
    if (tc_handle_is_invalid(h)) {
        tc_log(TC_LOG_ERROR, "tc_material_create: pool alloc failed");
        return tc_material_handle_invalid();
    }

    tc_material* mat = (tc_material*)tc_pool_get(&g_material_pool, h);
    memset(mat, 0, sizeof(tc_material));
    tc_resource_header_set_uuid(&mat->header, final_uuid, "tc_material_create");
    mat->header.name = tc_intern_string(name);
    mat->header.version = 1;
    mat->header.ref_count = 0;
    mat->header.is_loaded = 1;
    mat->self_handle = h;

    if (!tc_resource_map_add(g_material_uuid_to_index, mat->header.uuid, tc_pack_index(h.index))) {
        tc_log(TC_LOG_ERROR, "tc_material_create: failed to add to uuid map");
        tc_pool_free_slot(&g_material_pool, h);
        return tc_material_handle_invalid();
    }

    return h;
}

tc_material_handle tc_material_find(const char* uuid) {
    if (!g_material_initialized || !uuid) {
        return tc_material_handle_invalid();
    }

    void* ptr = tc_resource_map_get(g_material_uuid_to_index, uuid);
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
    if (!g_material_initialized || !name) {
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
    if (!g_material_initialized)
        return NULL;
    return (tc_material*)tc_pool_get_checked(&g_material_pool, h, "tc_material");
}

bool tc_material_is_valid(tc_material_handle h) {
    if (!g_material_initialized || tc_material_handle_is_invalid(h)) {
        return false;
    }
    return tc_pool_is_valid(&g_material_pool, h);
}

// Helper to release all shader references in a material
static void material_release_shaders(tc_material* mat) {
    if (!mat)
        return;
    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_shader* s = tc_shader_get(mat->phases[i].shader);
        if (s) {
            tc_shader_release(s);
        }
    }
}

bool tc_material_destroy(tc_material_handle h) {
    if (!g_material_initialized)
        return false;

    tc_material* mat = tc_material_get(h);
    if (!mat)
        return false;

    // Release all shader references before destroying
    material_release_shaders(mat);

    tc_resource_map_remove(g_material_uuid_to_index, mat->header.uuid);
    tc_pool_free_slot(&g_material_pool, h);

    return true;
}

bool tc_material_contains(const char* uuid) {
    return !tc_material_handle_is_invalid(tc_material_find(uuid));
}

size_t tc_material_count(void) {
    if (!g_material_initialized)
        return 0;
    return tc_pool_count(&g_material_pool);
}

const char* tc_material_get_uuid_str(tc_material_handle h) {
    tc_material* mat = tc_material_get(h);
    return mat ? mat->header.uuid : NULL;
}

const char* tc_material_get_name_str(tc_material_handle h) {
    tc_material* mat = tc_material_get(h);
    return mat ? mat->header.name : NULL;
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
    if (!mat || mat->header.ref_count == 0)
        return false;

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

tc_material_phase*
tc_material_add_phase(tc_material* mat, tc_shader_handle shader, const char* phase_mark, int priority) {
    if (!mat || mat->phase_count >= TC_MATERIAL_MAX_PHASES) {
        return NULL;
    }

    const char* resolved_name = phase_mark ? phase_mark : "opaque";
    tc_phase_mask resolved_phase = tc_phase_find(resolved_name);
    if (resolved_phase == TC_PHASE_NONE) {
        tc_log(TC_LOG_ERROR, "tc_material_add_phase: phase '%s' is not present in the project registry", resolved_name);
        return NULL;
    }

    // Add reference to shader (material owns it now)
    tc_shader* s = tc_shader_get(shader);
    if (s) {
        tc_shader_add_ref(s);
    }

    tc_material_phase* phase = &mat->phases[mat->phase_count];
    memset(phase, 0, sizeof(tc_material_phase));
    phase->owner_material = mat->self_handle;
    phase->owner_phase_index = mat->phase_count;

    phase->shader = shader;
    phase->phase = resolved_phase;
    phase->state = tc_render_state_opaque();
    phase->priority = priority;

    strncpy(phase->phase_mark, resolved_name, TC_PHASE_MARK_MAX - 1);

    mat->phase_count++;
    mat->header.version++;

    return phase;
}

bool tc_material_remove_phase(tc_material* mat, size_t index) {
    if (!mat || index >= mat->phase_count) {
        return false;
    }

    // Release shader reference for removed phase before the slot gets
    // overwritten by the shift.
    tc_shader* s = tc_shader_get(mat->phases[index].shader);
    if (s) {
        tc_shader_release(s);
    }

    for (size_t i = index; i < mat->phase_count - 1; i++) {
        mat->phases[i] = mat->phases[i + 1];
        mat->phases[i].owner_material = mat->self_handle;
        mat->phases[i].owner_phase_index = i;
    }

    mat->phase_count--;
    mat->header.version++;

    return true;
}

size_t
tc_material_get_phases_for_mark(tc_material* mat, const char* mark, tc_material_phase** out_phases, size_t max_count) {
    return tc_material_get_phases_for_phase(mat, tc_phase_find(mark), out_phases, max_count);
}

size_t tc_material_get_phases_for_phase(tc_material* mat,
                                        tc_phase_mask phase,
                                        tc_material_phase** out_phases,
                                        size_t max_count) {
    if (!mat || !tc_phase_is_single(phase) || !out_phases || max_count == 0) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < mat->phase_count && count < max_count; i++) {
        if (mat->phases[i].phase == phase) {
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

bool tc_material_find_phase_ref(const tc_material_phase* phase,
                                tc_material_handle* out_material,
                                size_t* out_phase_index) {
    if (out_material) {
        *out_material = tc_material_handle_invalid();
    }
    if (out_phase_index) {
        *out_phase_index = 0;
    }
    if (!g_material_initialized || !phase || tc_material_handle_is_invalid(phase->owner_material)) {
        return false;
    }

    tc_material* material = tc_material_get(phase->owner_material);
    if (!material || phase->owner_phase_index >= material->phase_count ||
        &material->phases[phase->owner_phase_index] != phase) {
        return false;
    }
    if (out_material) {
        *out_material = phase->owner_material;
    }
    if (out_phase_index) {
        *out_phase_index = phase->owner_phase_index;
    }
    return true;
}

// ============================================================================
// Phase uniform/texture operations
// ============================================================================

bool tc_material_phase_set_uniform(tc_material_phase* phase,
                                   const char* name,
                                   tc_uniform_type type,
                                   const void* value) {
    if (!phase || !name || !value)
        return false;
    if (strlen(name) >= TC_UNIFORM_NAME_MAX) {
        tc_log(TC_LOG_ERROR, "tc_material_phase_set_uniform: name '%s' exceeds fixed capacity", name);
        return false;
    }

    tc_uniform_value* uniform = tc_material_phase_find_uniform(phase, name);
    if (uniform && uniform->type != (uint8_t)type) {
        tc_log(TC_LOG_ERROR,
               "tc_material_phase_set_uniform: type mismatch for '%s' (existing=%u, requested=%u)",
               name,
               (unsigned)uniform->type,
               (unsigned)type);
        return false;
    }

    // Validate the type before allocating a new slot. This keeps failed
    // updates fully non-mutating, including uniform_count.
    switch (type) {
    case TC_UNIFORM_BOOL:
    case TC_UNIFORM_INT:
    case TC_UNIFORM_FLOAT:
    case TC_UNIFORM_VEC2:
    case TC_UNIFORM_VEC3:
    case TC_UNIFORM_VEC4:
    case TC_UNIFORM_MAT4:
    case TC_UNIFORM_SRGB_COLOR:
    case TC_UNIFORM_LINEAR_COLOR:
        break;
    default:
        tc_log(TC_LOG_ERROR, "tc_material_phase_set_uniform: unsupported type %u for '%s'", (unsigned)type, name);
        return false;
    }

    if (!uniform) {
        if (phase->uniform_count >= TC_MATERIAL_MAX_UNIFORMS) {
            tc_log(TC_LOG_ERROR, "tc_material_phase_set_uniform: uniform capacity exceeded for '%s'", name);
            return false;
        }
        uniform = &phase->uniforms[phase->uniform_count++];
        memset(uniform, 0, sizeof(*uniform));
        strncpy(uniform->name, name, TC_UNIFORM_NAME_MAX - 1);
        uniform->name[TC_UNIFORM_NAME_MAX - 1] = '\0';
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
    case TC_UNIFORM_SRGB_COLOR:
        memcpy(&uniform->data.srgb_color, value, sizeof(tc_srgb_color));
        break;
    case TC_UNIFORM_LINEAR_COLOR:
        memcpy(&uniform->data.linear_color, value, sizeof(tc_linear_color));
        break;
    case TC_UNIFORM_MAT4:
        memcpy(uniform->data.m4, value, sizeof(float) * 16);
        break;
    default:
        // Type was validated above; keep this defensive branch for future
        // additions to the enum.
        return false;
    }

    return true;
}

bool tc_material_phase_set_srgb_color(tc_material_phase* phase, const char* name, tc_srgb_color value) {
    return tc_material_phase_set_uniform(phase, name, TC_UNIFORM_SRGB_COLOR, &value);
}

bool tc_material_phase_set_linear_color(tc_material_phase* phase, const char* name, tc_linear_color value) {
    return tc_material_phase_set_uniform(phase, name, TC_UNIFORM_LINEAR_COLOR, &value);
}

bool tc_material_phase_get_srgb_color(const tc_material_phase* phase, const char* name, tc_srgb_color* out_value) {
    if (!phase || !name || !out_value)
        return false;
    for (size_t i = 0; i < phase->uniform_count; i++) {
        const tc_uniform_value* uniform = &phase->uniforms[i];
        if (strcmp(uniform->name, name) == 0) {
            if (uniform->type != TC_UNIFORM_SRGB_COLOR) {
                tc_log(TC_LOG_ERROR, "tc_material_phase_get_srgb_color: type mismatch for '%s'", name);
                return false;
            }
            *out_value = uniform->data.srgb_color;
            return true;
        }
    }
    return false;
}

bool tc_material_phase_get_linear_color(const tc_material_phase* phase, const char* name, tc_linear_color* out_value) {
    if (!phase || !name || !out_value)
        return false;
    for (size_t i = 0; i < phase->uniform_count; i++) {
        const tc_uniform_value* uniform = &phase->uniforms[i];
        if (strcmp(uniform->name, name) == 0) {
            if (uniform->type != TC_UNIFORM_LINEAR_COLOR) {
                tc_log(TC_LOG_ERROR, "tc_material_phase_get_linear_color: type mismatch for '%s'", name);
                return false;
            }
            *out_value = uniform->data.linear_color;
            return true;
        }
    }
    return false;
}

bool tc_material_phase_set_texture(tc_material_phase* phase, const char* name, tc_texture_handle texture) {
    if (!phase || !name)
        return false;
    if (!tc_material_phase_accepts_texture(phase, name, texture)) {
        return false;
    }

    tc_material_texture* tex = tc_material_phase_find_texture(phase, name);
    if (!tex) {
        if (phase->texture_count >= TC_MATERIAL_MAX_TEXTURES) {
            return false;
        }
        tex = &phase->textures[phase->texture_count++];
        strncpy(tex->name, name, TC_UNIFORM_NAME_MAX - 1);
        tex->name[TC_UNIFORM_NAME_MAX - 1] = '\0';
    }

    if (tex->has_expected_encoding && !tc_texture_handle_is_invalid(texture)) {
        const tc_texture* candidate = tc_texture_get(texture);
        if (candidate && candidate->encoding != tex->expected_encoding) {
            tc_log(TC_LOG_WARN,
                   "tc_material_phase_set_texture: slot '%s' expects %s but "
                   "texture '%s' is %s; binding it unchanged",
                   name,
                   tex->expected_encoding == TC_TEXTURE_ENCODING_SRGB ? "sRGB" : "Linear",
                   candidate->header.name ? candidate->header.name : candidate->header.uuid,
                   candidate->encoding == TC_TEXTURE_ENCODING_SRGB ? "sRGB" : "Linear");
        }
    }

    tex->texture = texture;
    return true;
}

bool tc_material_phase_declare_texture(tc_material_phase* phase,
                                       const char* name,
                                       tc_texture_encoding expected_encoding) {
    if (!phase || !name || name[0] == '\0') {
        tc_log(TC_LOG_ERROR, "tc_material_phase_declare_texture: phase and name are required");
        return false;
    }
    if (expected_encoding != TC_TEXTURE_ENCODING_LINEAR && expected_encoding != TC_TEXTURE_ENCODING_SRGB) {
        tc_log(TC_LOG_ERROR,
               "tc_material_phase_declare_texture: unsupported encoding %u for slot '%s'",
               (unsigned)expected_encoding,
               name);
        return false;
    }

    if (!tc_material_phase_declare_texture_slot(phase, name))
        return false;
    tc_material_texture* slot = tc_material_phase_find_texture(phase, name);
    if (slot->has_expected_encoding && slot->expected_encoding != (uint8_t)expected_encoding) {
        tc_log(TC_LOG_ERROR, "tc_material_phase_declare_texture: conflicting encoding contract for slot '%s'", name);
        return false;
    }

    if (!tc_texture_handle_is_invalid(slot->texture)) {
        tc_texture* texture = tc_texture_get(slot->texture);
        if (!texture) {
            tc_log(TC_LOG_ERROR,
                   "tc_material_phase_declare_texture: stale existing texture "
                   "for slot '%s'",
                   name);
            return false;
        }
        if (texture->encoding != (uint8_t)expected_encoding) {
            tc_log(TC_LOG_WARN,
                   "tc_material_phase_declare_texture: existing texture for slot '%s' "
                   "does not match %s encoding; keeping the binding unchanged",
                   name,
                   expected_encoding == TC_TEXTURE_ENCODING_SRGB ? "sRGB" : "Linear");
        }
    }

    slot->has_expected_encoding = 1;
    slot->expected_encoding = (uint8_t)expected_encoding;
    return true;
}

bool tc_material_phase_declare_texture_slot(tc_material_phase* phase, const char* name) {
    if (!phase || !name || name[0] == '\0') {
        tc_log(TC_LOG_ERROR, "tc_material_phase_declare_texture_slot: phase and name are required");
        return false;
    }
    tc_material_texture* slot = tc_material_phase_find_texture(phase, name);
    if (!slot) {
        if (phase->texture_count >= TC_MATERIAL_MAX_TEXTURES) {
            tc_log(
                TC_LOG_ERROR, "tc_material_phase_declare_texture_slot: texture slot capacity exceeded for '%s'", name);
            return false;
        }
        slot = &phase->textures[phase->texture_count++];
        memset(slot, 0, sizeof(*slot));
        slot->texture = tc_texture_handle_invalid();
        strncpy(slot->name, name, TC_UNIFORM_NAME_MAX - 1);
        slot->name[TC_UNIFORM_NAME_MAX - 1] = '\0';
    }
    slot->is_declared = 1;
    return true;
}

bool tc_material_phase_accepts_texture(const tc_material_phase* phase, const char* name, tc_texture_handle texture) {
    if (!phase || !name || name[0] == '\0')
        return false;
    const tc_material_texture* slot = NULL;
    for (size_t i = 0; i < phase->texture_count; ++i) {
        if (strcmp(phase->textures[i].name, name) == 0) {
            slot = &phase->textures[i];
            break;
        }
    }
    if (!slot) {
        if (phase->texture_count >= TC_MATERIAL_MAX_TEXTURES) {
            tc_log(TC_LOG_ERROR, "tc_material_phase_set_texture: texture slot capacity exceeded for '%s'", name);
            return false;
        }
        return true;
    }
    if (!slot->has_expected_encoding || tc_texture_handle_is_invalid(texture)) {
        return true;
    }

    const tc_texture* candidate = tc_texture_get(texture);
    if (!candidate) {
        tc_log(TC_LOG_ERROR, "tc_material_phase_set_texture: stale texture handle for slot '%s'", name);
        return false;
    }
    if (candidate->encoding != slot->expected_encoding) {
        // Encoding is a semantic diagnostic, not a safety boundary. The
        // texture's own encoding still selects its native GPU format and
        // sampling behavior, so an incompatible slot remains renderable.
        return true;
    }
    return true;
}

void tc_material_phase_make_transparent(tc_material_phase* phase) {
    if (!phase)
        return;
    phase->state = tc_render_state_transparent();
}

// ============================================================================
// Material uniform/texture operations
// ============================================================================

tc_material_phase* tc_material_find_phase(tc_material* mat, const char* mark) {
    if (!mat || !mark)
        return NULL;

    for (size_t i = 0; i < mat->phase_count; i++) {
        if (strcmp(mat->phases[i].phase_mark, mark) == 0) {
            return &mat->phases[i];
        }
    }
    return NULL;
}

void tc_material_set_uniform(tc_material* mat, const char* name, tc_uniform_type type, const void* value) {
    if (!mat)
        return;

    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_material_phase_set_uniform(&mat->phases[i], name, type, value);
    }
    // Note: uniforms are per-frame values, don't bump version
}

bool tc_material_set_srgb_color(tc_material* mat, const char* name, tc_srgb_color value) {
    if (!mat || !name)
        return false;
    if (strlen(name) >= TC_UNIFORM_NAME_MAX) {
        tc_log(TC_LOG_ERROR, "tc_material_set_srgb_color: name exceeds fixed capacity");
        return false;
    }
    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_uniform_value* uniform = tc_material_phase_find_uniform(&mat->phases[i], name);
        if (uniform && uniform->type != TC_UNIFORM_SRGB_COLOR) {
            tc_log(TC_LOG_ERROR, "tc_material_set_srgb_color: type mismatch for '%s'", name);
            return false;
        }
        if (!uniform && mat->phases[i].uniform_count >= TC_MATERIAL_MAX_UNIFORMS) {
            tc_log(TC_LOG_ERROR, "tc_material_set_srgb_color: uniform capacity exceeded for '%s'", name);
            return false;
        }
    }
    bool updated = false;
    for (size_t i = 0; i < mat->phase_count; i++) {
        if (!tc_material_phase_set_srgb_color(&mat->phases[i], name, value))
            return false;
        updated = true;
    }
    return updated;
}

bool tc_material_set_linear_color(tc_material* mat, const char* name, tc_linear_color value) {
    if (!mat || !name)
        return false;
    if (strlen(name) >= TC_UNIFORM_NAME_MAX) {
        tc_log(TC_LOG_ERROR, "tc_material_set_linear_color: name exceeds fixed capacity");
        return false;
    }
    for (size_t i = 0; i < mat->phase_count; i++) {
        tc_uniform_value* uniform = tc_material_phase_find_uniform(&mat->phases[i], name);
        if (uniform && uniform->type != TC_UNIFORM_LINEAR_COLOR) {
            tc_log(TC_LOG_ERROR, "tc_material_set_linear_color: type mismatch for '%s'", name);
            return false;
        }
        if (!uniform && mat->phases[i].uniform_count >= TC_MATERIAL_MAX_UNIFORMS) {
            tc_log(TC_LOG_ERROR, "tc_material_set_linear_color: uniform capacity exceeded for '%s'", name);
            return false;
        }
    }
    bool updated = false;
    for (size_t i = 0; i < mat->phase_count; i++) {
        if (!tc_material_phase_set_linear_color(&mat->phases[i], name, value))
            return false;
        updated = true;
    }
    return updated;
}

bool tc_material_get_srgb_color(const tc_material* mat, const char* name, tc_srgb_color* out_value) {
    if (!mat || mat->phase_count == 0)
        return false;
    return tc_material_phase_get_srgb_color(&mat->phases[0], name, out_value);
}

bool tc_material_get_linear_color(const tc_material* mat, const char* name, tc_linear_color* out_value) {
    if (!mat || mat->phase_count == 0)
        return false;
    return tc_material_phase_get_linear_color(&mat->phases[0], name, out_value);
}

size_t tc_material_set_texture(tc_material* mat, const char* name, tc_texture_handle texture) {
    if (!mat || !name || mat->phase_count == 0)
        return 0;

    bool has_declared_schema = false;
    bool target_is_declared = false;
    for (size_t phase_index = 0; phase_index < mat->phase_count; ++phase_index) {
        const tc_material_phase* phase = &mat->phases[phase_index];
        for (size_t slot_index = 0; slot_index < phase->texture_count; ++slot_index) {
            const tc_material_texture* slot = &phase->textures[slot_index];
            if (!slot->is_declared)
                continue;
            has_declared_schema = true;
            if (strcmp(slot->name, name) == 0) {
                target_is_declared = true;
            }
        }
    }
    if (has_declared_schema && !target_is_declared) {
        tc_log(TC_LOG_ERROR, "tc_material_set_texture: slot '%s' is not present in canonical schema", name);
        return 0;
    }

    bool has_material_slot = false;
    for (size_t i = 0; i < mat->texture_handle_count; ++i) {
        if (strcmp(mat->texture_handles[i].name, name) == 0) {
            has_material_slot = true;
            break;
        }
    }
    if (!has_material_slot && mat->texture_handle_count >= TC_MATERIAL_MAX_TEXTURES) {
        tc_log(TC_LOG_ERROR, "tc_material_set_texture: inspector texture slot capacity exceeded for '%s'", name);
        return 0;
    }

    for (size_t i = 0; i < mat->phase_count; i++) {
        if (!tc_material_phase_accepts_texture(&mat->phases[i], name, texture)) {
            return 0;
        }
    }
    for (size_t i = 0; i < mat->phase_count; i++) {
        if (!tc_material_phase_set_texture(&mat->phases[i], name, texture)) {
            tc_log(TC_LOG_ERROR, "tc_material_set_texture: failed to bind slot '%s' after validation", name);
            return 0;
        }
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
        memset(th, 0, sizeof(*th));
        strncpy(th->name, name, TC_UNIFORM_NAME_MAX - 1);
        th->name[TC_UNIFORM_NAME_MAX - 1] = '\0';
    }
    if (th) {
        th->texture = texture;
    }

    // An explicit ordinary texture assignment replaces a symbolic source.
    tc_material_clear_texture_source(mat, name);

    mat->header.version++;
    return mat->phase_count;
}

const tc_material_texture_source* tc_material_find_texture_source(const tc_material* mat, const char* uniform_name) {
    if (!mat || !uniform_name)
        return NULL;
    for (size_t i = 0; i < mat->texture_source_count; ++i) {
        if (strcmp(mat->texture_sources[i].uniform_name, uniform_name) == 0)
            return &mat->texture_sources[i];
    }
    return NULL;
}

bool tc_material_clear_texture_source(tc_material* mat, const char* uniform_name) {
    if (!mat || !uniform_name)
        return false;
    for (size_t i = 0; i < mat->texture_source_count; ++i) {
        if (strcmp(mat->texture_sources[i].uniform_name, uniform_name) != 0)
            continue;
        if (i + 1 < mat->texture_source_count) {
            memmove(&mat->texture_sources[i],
                    &mat->texture_sources[i + 1],
                    (mat->texture_source_count - i - 1) * sizeof(mat->texture_sources[0]));
        }
        --mat->texture_source_count;
        memset(&mat->texture_sources[mat->texture_source_count], 0, sizeof(mat->texture_sources[0]));
        mat->header.version++;
        return true;
    }
    return false;
}

bool tc_material_set_texture_source(
    tc_material* mat, const char* uniform_name, const char* kind, const char* source_name, const char* channel) {
    if (!mat || !uniform_name || !uniform_name[0] || !kind || !kind[0] || !source_name || !source_name[0] || !channel ||
        !channel[0]) {
        tc_log(TC_LOG_ERROR, "tc_material_set_texture_source: all fields are required");
        return false;
    }
    if (strlen(uniform_name) >= TC_UNIFORM_NAME_MAX || strlen(kind) >= TC_MATERIAL_TEXTURE_SOURCE_KIND_MAX ||
        strlen(source_name) >= TC_MATERIAL_TEXTURE_SOURCE_NAME_MAX ||
        strlen(channel) >= TC_MATERIAL_TEXTURE_SOURCE_CHANNEL_MAX) {
        tc_log(TC_LOG_ERROR, "tc_material_set_texture_source: one or more fields exceed their fixed capacity");
        return false;
    }

    bool declared = false;
    for (size_t phase_index = 0; phase_index < mat->phase_count && !declared; ++phase_index) {
        const tc_material_texture* slot = tc_material_phase_find_texture(&mat->phases[phase_index], uniform_name);
        declared = slot && slot->is_declared;
    }
    if (!declared) {
        tc_log(
            TC_LOG_ERROR, "tc_material_set_texture_source: slot '%s' is not present in canonical schema", uniform_name);
        return false;
    }

    tc_material_texture_source* source = NULL;
    for (size_t i = 0; i < mat->texture_source_count; ++i) {
        if (strcmp(mat->texture_sources[i].uniform_name, uniform_name) == 0) {
            source = &mat->texture_sources[i];
            break;
        }
    }
    if (!source) {
        if (mat->texture_source_count >= TC_MATERIAL_MAX_TEXTURES) {
            tc_log(TC_LOG_ERROR, "tc_material_set_texture_source: source capacity exceeded for '%s'", uniform_name);
            return false;
        }
        source = &mat->texture_sources[mat->texture_source_count++];
    }
    memset(source, 0, sizeof(*source));
    strncpy(source->uniform_name, uniform_name, sizeof(source->uniform_name) - 1);
    strncpy(source->kind, kind, sizeof(source->kind) - 1);
    strncpy(source->source_name, source_name, sizeof(source->source_name) - 1);
    strncpy(source->channel, channel, sizeof(source->channel) - 1);
    mat->header.version++;
    return true;
}

// ============================================================================
// Info collection
// ============================================================================

tc_material_info* tc_material_get_all_info(size_t* count) {
    if (!g_material_initialized || !count) {
        if (count)
            *count = 0;
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
        if (g_material_pool.states[i] != TC_SLOT_OCCUPIED)
            continue;

        tc_material* mat = (tc_material*)tc_pool_get_unchecked(&g_material_pool, i);
        if (!mat)
            continue;

        infos[idx].handle.index = i;
        infos[idx].handle.generation = g_material_pool.generations[i];
        strncpy(infos[idx].uuid, mat->header.uuid, sizeof(infos[idx].uuid));
        infos[idx].name = mat->header.name;
        infos[idx].ref_count = mat->header.ref_count;
        infos[idx].version = mat->header.version;
        infos[idx].phase_count = mat->phase_count;
        infos[idx].texture_count = mat->texture_handle_count;
        infos[idx].is_loaded = mat->header.is_loaded;
        idx++;
    }

    *count = idx;
    return infos;
}

// ============================================================================
// Iteration
// ============================================================================

void tc_material_foreach(tc_material_iter_fn callback, void* user_data) {
    if (!g_material_initialized || !callback)
        return;

    for (uint32_t i = 0; i < g_material_pool.capacity; i++) {
        if (g_material_pool.states[i] != TC_SLOT_OCCUPIED)
            continue;

        tc_material* mat = (tc_material*)tc_pool_get_unchecked(&g_material_pool, i);
        if (!mat)
            continue;

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
        tc_log(TC_LOG_ERROR, "[tc_material_copy] source material '%s' has no name", src_mat->header.uuid);
        return tc_material_handle_invalid();
    }

    // Generate name for copy
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "%s_copy", src_mat->header.name);

    tc_material_handle dst = tc_material_create(new_uuid, name_buf);
    // tc_material_create can grow the pool and move existing materials.
    // Re-fetch the source by handle before reading it again.
    src_mat = tc_material_get(src);
    if (!src_mat) {
        if (!tc_material_handle_is_invalid(dst)) {
            tc_material_destroy(dst);
        }
        return tc_material_handle_invalid();
    }

    tc_material* dst_mat = tc_material_get(dst);
    if (!dst_mat) {
        return tc_material_handle_invalid();
    }

    // Copy phases and add shader references
    dst_mat->phase_count = src_mat->phase_count;
    for (size_t i = 0; i < src_mat->phase_count; i++) {
        dst_mat->phases[i] = src_mat->phases[i];
        dst_mat->phases[i].owner_material = dst;
        dst_mat->phases[i].owner_phase_index = i;
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
    dst_mat->texture_source_count = src_mat->texture_source_count;
    for (size_t i = 0; i < src_mat->texture_source_count; ++i) {
        dst_mat->texture_sources[i] = src_mat->texture_sources[i];
    }

    // Copy metadata
    strncpy(dst_mat->shader_name, src_mat->shader_name, TC_MATERIAL_NAME_MAX - 1);
    strncpy(dst_mat->shader_program_uuid, src_mat->shader_program_uuid, TC_UUID_SIZE - 1);
    dst_mat->shader_program_version = src_mat->shader_program_version;
    strncpy(dst_mat->active_phase_mark, src_mat->active_phase_mark, TC_PHASE_MARK_MAX - 1);

    return dst;
}
