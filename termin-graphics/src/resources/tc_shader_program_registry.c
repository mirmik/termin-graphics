#include <tgfx/resources/tc_shader_program_registry.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>
#include <tcbase/tc_registry_utils.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tc_string.h>
#include <tgfx/resources/tc_shader_registry.h>

static tc_pool g_program_pool;
static tc_resource_map* g_program_uuid_to_index = NULL;
static bool g_program_initialized = false;

static bool copy_required_string(
    char* destination,
    size_t capacity,
    const char* source,
    const char* field_name
) {
    if (!source || source[0] == '\0') {
        tc_log_error("tc_shader_program_set_payload: %s is required", field_name);
        return false;
    }
    const size_t length = strlen(source);
    if (length >= capacity) {
        tc_log_error(
            "tc_shader_program_set_payload: %s '%s' exceeds %zu bytes",
            field_name,
            source,
            capacity - 1);
        return false;
    }
    memcpy(destination, source, length + 1);
    return true;
}

static void release_phases(tc_shader_program_phase* phases, uint32_t count) {
    if (!phases) return;
    for (uint32_t i = 0; i < count; ++i) {
        tc_shader* shader = tc_shader_get(phases[i].shader);
        if (shader) tc_shader_release(shader);
    }
    free(phases);
}

static void clear_payload(tc_shader_program* program) {
    if (!program) return;
    release_phases(program->phases, program->phase_count);
    free(program->properties);
    program->properties = NULL;
    program->property_count = 0;
    program->phases = NULL;
    program->phase_count = 0;
}

void tc_shader_program_init(void) {
    TC_REGISTRY_INIT_GUARD(g_program_initialized, "tc_shader_program");
    if (!tc_pool_init(&g_program_pool, sizeof(tc_shader_program), 32)) {
        tc_log_error("tc_shader_program_init: failed to initialize pool");
        return;
    }
    g_program_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_program_uuid_to_index) {
        tc_log_error("tc_shader_program_init: failed to create UUID map");
        tc_pool_free(&g_program_pool);
        return;
    }
    g_program_initialized = true;
}

void tc_shader_program_shutdown(void) {
    TC_REGISTRY_SHUTDOWN_GUARD(g_program_initialized, "tc_shader_program");
    for (uint32_t i = 0; i < g_program_pool.capacity; ++i) {
        if (g_program_pool.states[i] == TC_SLOT_OCCUPIED) {
            clear_payload((tc_shader_program*)tc_pool_get_unchecked(&g_program_pool, i));
        }
    }
    tc_pool_free(&g_program_pool);
    tc_resource_map_free(g_program_uuid_to_index);
    g_program_uuid_to_index = NULL;
    g_program_initialized = false;
}

tc_shader_program_handle tc_shader_program_create(const char* uuid, const char* name) {
    if (!g_program_initialized) tc_shader_program_init();
    if (!uuid || uuid[0] == '\0' || !name || name[0] == '\0') {
        tc_log_error("tc_shader_program_create: UUID and name are required");
        return tc_shader_program_handle_invalid();
    }
    if (strlen(uuid) >= TC_UUID_SIZE) {
        tc_log_error("tc_shader_program_create: UUID '%s' exceeds %u bytes", uuid, TC_UUID_SIZE - 1);
        return tc_shader_program_handle_invalid();
    }
    if (tc_shader_program_contains(uuid)) {
        tc_log_warn("tc_shader_program_create: UUID '%s' already exists", uuid);
        return tc_shader_program_handle_invalid();
    }

    tc_handle raw = tc_pool_alloc(&g_program_pool);
    if (tc_handle_is_invalid(raw)) {
        tc_log_error("tc_shader_program_create: pool allocation failed");
        return tc_shader_program_handle_invalid();
    }
    tc_shader_program* program = (tc_shader_program*)tc_pool_get(&g_program_pool, raw);
    memset(program, 0, sizeof(*program));
    tc_resource_header_init(&program->header, uuid);
    program->header.name = tc_intern_string(name);
    program->header.is_loaded = 1;
    program->header.pool_index = raw.index;
    program->self_handle = raw;
    if (!tc_resource_map_add(g_program_uuid_to_index, uuid, tc_pack_index(raw.index))) {
        tc_log_error("tc_shader_program_create: failed to publish UUID '%s'", uuid);
        tc_pool_free_slot(&g_program_pool, raw);
        return tc_shader_program_handle_invalid();
    }
    return raw;
}

tc_shader_program_handle tc_shader_program_find(const char* uuid) {
    if (!g_program_initialized || !uuid) return tc_shader_program_handle_invalid();
    void* value = tc_resource_map_get(g_program_uuid_to_index, uuid);
    if (!tc_has_index(value)) return tc_shader_program_handle_invalid();
    const uint32_t index = tc_unpack_index(value);
    if (index >= g_program_pool.capacity || g_program_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_shader_program_handle_invalid();
    }
    tc_shader_program_handle result = {index, g_program_pool.generations[index]};
    return result;
}

tc_shader_program_handle tc_shader_program_declare(const char* uuid, const char* name) {
    tc_shader_program_handle existing = tc_shader_program_find(uuid);
    if (!tc_shader_program_handle_is_invalid(existing)) return existing;
    return tc_shader_program_create(uuid, name);
}

tc_shader_program_handle tc_shader_program_get_or_create(const char* uuid, const char* name) {
    return tc_shader_program_declare(uuid, name);
}

tc_shader_program* tc_shader_program_get(tc_shader_program_handle handle) {
    if (!g_program_initialized) return NULL;
    return (tc_shader_program*)tc_pool_get_checked(
        &g_program_pool, handle, "tc_shader_program");
}

bool tc_shader_program_is_valid(tc_shader_program_handle handle) {
    return g_program_initialized && tc_pool_is_valid(&g_program_pool, handle);
}

bool tc_shader_program_contains(const char* uuid) {
    return !tc_shader_program_handle_is_invalid(tc_shader_program_find(uuid));
}

size_t tc_shader_program_count(void) {
    return g_program_initialized ? tc_pool_count(&g_program_pool) : 0;
}

tc_shader_program_info* tc_shader_program_get_all_info(size_t* count) {
    if (!count) return NULL;
    *count = 0;
    const size_t program_count = tc_shader_program_count();
    if (program_count == 0) return NULL;

    tc_shader_program_info* infos =
        (tc_shader_program_info*)calloc(program_count, sizeof(*infos));
    if (!infos) {
        tc_log_error("tc_shader_program_get_all_info: allocation failed");
        return NULL;
    }

    size_t output_index = 0;
    for (uint32_t index = 0;
         index < g_program_pool.capacity && output_index < program_count;
         ++index) {
        if (g_program_pool.states[index] != TC_SLOT_OCCUPIED) continue;
        tc_shader_program* program =
            (tc_shader_program*)tc_pool_get_unchecked(&g_program_pool, index);
        tc_shader_program_info* info = &infos[output_index++];
        info->handle.index = index;
        info->handle.generation = g_program_pool.generations[index];
        memcpy(info->uuid, program->header.uuid, sizeof(info->uuid));
        info->name = program->header.name;
        info->source_path = program->source_path;
        info->language = program->language;
        info->ref_count = program->header.ref_count;
        info->version = program->header.version;
        info->property_count = program->property_count;
        info->phase_count = program->phase_count;
        info->is_loaded = program->header.is_loaded;
    }
    *count = output_index;
    return infos;
}

static bool destroy_program(tc_shader_program_handle handle) {
    tc_shader_program* program = tc_shader_program_get(handle);
    if (!program) return false;
    tc_resource_map_remove(g_program_uuid_to_index, program->header.uuid);
    clear_payload(program);
    tc_pool_free_slot(&g_program_pool, handle);
    return true;
}

void tc_shader_program_retain(tc_shader_program* program) {
    if (program) ++program->header.ref_count;
}

bool tc_shader_program_release(tc_shader_program* program) {
    if (!program || program->header.ref_count == 0) return false;
    --program->header.ref_count;
    if (program->header.ref_count != 0) return false;
    return destroy_program(program->self_handle);
}

bool tc_shader_program_remove(tc_shader_program_handle handle) {
    tc_shader_program* program = tc_shader_program_get(handle);
    if (!program) return false;
    if (program->header.ref_count != 0) {
        tc_log_error(
            "tc_shader_program_remove: '%s' still has %u retained handle(s)",
            program->header.uuid,
            program->header.ref_count);
        return false;
    }
    return destroy_program(handle);
}

static uint64_t fnv1a_append(uint64_t hash, const char* text) {
    if (!text) return hash;
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

void tc_shader_program_make_phase_uuid(
    char* out_uuid,
    size_t out_size,
    const char* program_uuid,
    const char* phase_mark
) {
    if (!out_uuid || out_size == 0) return;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    hash = fnv1a_append(hash, program_uuid);
    hash = fnv1a_append(hash, "::phase::");
    hash = fnv1a_append(hash, phase_mark);
    snprintf(out_uuid, out_size, "shader-phase-%016llx", (unsigned long long)hash);
}

static bool build_properties(
    const tc_shader_program_payload_desc* desc,
    tc_shader_program_property** out_properties
) {
    *out_properties = NULL;
    if (desc->property_count == 0) return true;
    if (!desc->properties) {
        tc_log_error("tc_shader_program_set_payload: properties pointer is NULL");
        return false;
    }
    tc_shader_program_property* properties =
        (tc_shader_program_property*)calloc(desc->property_count, sizeof(*properties));
    if (!properties) {
        tc_log_error("tc_shader_program_set_payload: property allocation failed");
        return false;
    }
    for (uint32_t i = 0; i < desc->property_count; ++i) {
        const tc_shader_program_property_desc* input = &desc->properties[i];
        tc_shader_program_property* output = &properties[i];
        if (!copy_required_string(output->name, sizeof(output->name), input->name, "property name")
            || !copy_required_string(
                output->property_type,
                sizeof(output->property_type),
                input->property_type,
                "property type")) {
            free(properties);
            return false;
        }
        for (uint32_t j = 0; j < i; ++j) {
            if (strcmp(properties[j].name, output->name) == 0) {
                tc_log_error("tc_shader_program_set_payload: duplicate property '%s'", output->name);
                free(properties);
                return false;
            }
        }
        if (input->label && input->label[0] != '\0') {
            if (strlen(input->label) >= sizeof(output->label)) {
                tc_log_error("tc_shader_program_set_payload: property label is too long");
                free(properties);
                return false;
            }
            strcpy(output->label, input->label);
        }
        if (input->default_value) {
            output->default_value = *input->default_value;
            output->has_default = 1;
        } else if (input->default_text) {
            if (strlen(input->default_text) >= sizeof(output->default_text)) {
                tc_log_error("tc_shader_program_set_payload: property default text is too long");
                free(properties);
                return false;
            }
            strcpy(output->default_text, input->default_text);
            output->has_default = 1;
        }
        output->range_min = input->range_min;
        output->range_max = input->range_max;
        output->has_range_min = input->has_range_min;
        output->has_range_max = input->has_range_max;
    }
    *out_properties = properties;
    return true;
}

static bool build_phases(
    const tc_shader_program* program,
    const tc_shader_program_payload_desc* desc,
    tc_shader_program_phase** out_phases
) {
    *out_phases = NULL;
    if (desc->phase_count == 0) return true;
    if (!desc->phases) {
        tc_log_error("tc_shader_program_set_payload: phases pointer is NULL");
        return false;
    }
    tc_shader_program_phase* phases =
        (tc_shader_program_phase*)calloc(desc->phase_count, sizeof(*phases));
    if (!phases) {
        tc_log_error("tc_shader_program_set_payload: phase allocation failed");
        return false;
    }
    for (uint32_t i = 0; i < desc->phase_count; ++i) {
        const tc_shader_program_phase_desc* input = &desc->phases[i];
        tc_shader_program_phase* output = &phases[i];
        if (!copy_required_string(
                output->phase_mark,
                sizeof(output->phase_mark),
                input->phase_mark,
                "phase mark")) {
            release_phases(phases, i);
            return false;
        }
        for (uint32_t j = 0; j < i; ++j) {
            if (strcmp(phases[j].phase_mark, output->phase_mark) == 0) {
                tc_log_error(
                    "tc_shader_program_set_payload: duplicate phase '%s'",
                    output->phase_mark);
                release_phases(phases, i);
                return false;
            }
        }
        char shader_uuid[TC_UUID_SIZE];
        tc_shader_program_make_phase_uuid(
            shader_uuid,
            sizeof(shader_uuid),
            program->header.uuid,
            output->phase_mark);
        output->shader = tc_shader_get_or_create(shader_uuid);
        tc_shader* shader = tc_shader_get(output->shader);
        if (!shader) {
            tc_log_error(
                "tc_shader_program_set_payload: failed to declare shader for phase '%s'",
                output->phase_mark);
            release_phases(phases, i);
            return false;
        }
        tc_shader_add_ref(shader);
        output->priority = input->priority;
        output->state = input->state;
    }
    *out_phases = phases;
    return true;
}

bool tc_shader_program_set_payload(
    tc_shader_program* program,
    const tc_shader_program_payload_desc* desc
) {
    if (!program || !desc || !desc->name || desc->name[0] == '\0') {
        tc_log_error("tc_shader_program_set_payload: program and payload name are required");
        return false;
    }
    tc_shader_program_property* properties = NULL;
    tc_shader_program_phase* phases = NULL;
    if (!build_properties(desc, &properties)) return false;
    if (!build_phases(program, desc, &phases)) {
        free(properties);
        return false;
    }

    clear_payload(program);
    program->header.name = tc_intern_string(desc->name);
    program->source_path =
        desc->source_path && desc->source_path[0] ? tc_intern_string(desc->source_path) : NULL;
    program->language =
        desc->language && desc->language[0] ? tc_intern_string(desc->language) : NULL;
    program->features = desc->features;
    program->properties = properties;
    program->property_count = desc->property_count;
    program->phases = phases;
    program->phase_count = desc->phase_count;
    ++program->header.version;
    return true;
}

uint32_t tc_shader_program_version(const tc_shader_program* program) {
    return program ? program->header.version : 0;
}

const tc_shader_program_phase* tc_shader_program_find_phase(
    const tc_shader_program* program,
    const char* phase_mark
) {
    if (!program || !phase_mark) return NULL;
    for (uint32_t i = 0; i < program->phase_count; ++i) {
        if (strcmp(program->phases[i].phase_mark, phase_mark) == 0) return &program->phases[i];
    }
    return NULL;
}
