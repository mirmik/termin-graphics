#include <render/tc_pipeline_template_registry.h>

#include <stdlib.h>
#include <string.h>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>
#include <tcbase/tc_registry_utils.h>
#include <tcbase/tc_resource_map.h>
#include <tcbase/tc_string.h>

static tc_pool g_pool;
static tc_resource_map* g_uuid_to_index = NULL;
static bool g_initialized = false;

static const char* intern_optional(const char* value) {
    return value && value[0] ? tc_intern_string(value) : NULL;
}

static void clear_payload(tc_pipeline_template* pipeline_template) {
    if (!pipeline_template) return;
    free(pipeline_template->passes);
    free(pipeline_template->resources);
    free(pipeline_template->dependencies);
    free(pipeline_template->targets);
    pipeline_template->passes = NULL;
    pipeline_template->resources = NULL;
    pipeline_template->dependencies = NULL;
    pipeline_template->targets = NULL;
    pipeline_template->pass_count = 0;
    pipeline_template->resource_count = 0;
    pipeline_template->dependency_count = 0;
    pipeline_template->target_count = 0;
}

void tc_pipeline_template_init(void) {
    if (g_initialized) return;
    if (!tc_pool_init(&g_pool, sizeof(tc_pipeline_template), 32)) {
        tc_log_error("tc_pipeline_template_init: failed to initialize pool");
        return;
    }
    g_uuid_to_index = tc_resource_map_new(NULL);
    if (!g_uuid_to_index) {
        tc_log_error("tc_pipeline_template_init: failed to create UUID map");
        tc_pool_free(&g_pool);
        return;
    }
    g_initialized = true;
}

void tc_pipeline_template_shutdown(void) {
    if (!g_initialized) return;
    for (uint32_t i = 0; i < g_pool.capacity; ++i) {
        if (g_pool.states[i] == TC_SLOT_OCCUPIED) {
            clear_payload((tc_pipeline_template*)tc_pool_get_unchecked(&g_pool, i));
        }
    }
    tc_pool_free(&g_pool);
    tc_resource_map_free(g_uuid_to_index);
    g_uuid_to_index = NULL;
    g_initialized = false;
}

tc_pipeline_template_handle tc_pipeline_template_create(const char* uuid, const char* name) {
    if (!g_initialized) tc_pipeline_template_init();
    if (!g_initialized || !uuid || !uuid[0] || !name || !name[0]) {
        tc_log_error("tc_pipeline_template_create: UUID and name are required");
        return tc_pipeline_template_handle_invalid();
    }
    if (strlen(uuid) >= TC_UUID_SIZE) {
        tc_log_error("tc_pipeline_template_create: UUID '%s' is too long", uuid);
        return tc_pipeline_template_handle_invalid();
    }
    if (!tc_pipeline_template_handle_is_invalid(tc_pipeline_template_find(uuid))) {
        tc_log_warn("tc_pipeline_template_create: UUID '%s' already exists", uuid);
        return tc_pipeline_template_handle_invalid();
    }
    tc_handle raw = tc_pool_alloc(&g_pool);
    if (tc_handle_is_invalid(raw)) {
        tc_log_error("tc_pipeline_template_create: pool allocation failed");
        return tc_pipeline_template_handle_invalid();
    }
    tc_pipeline_template* pipeline_template = (tc_pipeline_template*)tc_pool_get(&g_pool, raw);
    memset(pipeline_template, 0, sizeof(*pipeline_template));
    tc_resource_header_init(&pipeline_template->header, uuid);
    pipeline_template->header.name = tc_intern_string(name);
    pipeline_template->header.is_loaded = 1;
    pipeline_template->header.pool_index = raw.index;
    pipeline_template->self_handle = raw;
    pipeline_template->descriptor_version = TC_PIPELINE_TEMPLATE_DESCRIPTOR_VERSION;
    if (!tc_resource_map_add(g_uuid_to_index, uuid, tc_pack_index(raw.index))) {
        tc_log_error("tc_pipeline_template_create: failed to publish UUID '%s'", uuid);
        tc_pool_free_slot(&g_pool, raw);
        return tc_pipeline_template_handle_invalid();
    }
    return raw;
}

tc_pipeline_template_handle tc_pipeline_template_find(const char* uuid) {
    if (!g_initialized || !uuid) return tc_pipeline_template_handle_invalid();
    void* value = tc_resource_map_get(g_uuid_to_index, uuid);
    if (!tc_has_index(value)) return tc_pipeline_template_handle_invalid();
    uint32_t index = tc_unpack_index(value);
    if (index >= g_pool.capacity || g_pool.states[index] != TC_SLOT_OCCUPIED) {
        return tc_pipeline_template_handle_invalid();
    }
    tc_pipeline_template_handle result = {index, g_pool.generations[index]};
    return result;
}

tc_pipeline_template_handle tc_pipeline_template_declare(const char* uuid, const char* name) {
    tc_pipeline_template_handle existing = tc_pipeline_template_find(uuid);
    return tc_pipeline_template_handle_is_invalid(existing)
        ? tc_pipeline_template_create(uuid, name) : existing;
}

tc_pipeline_template* tc_pipeline_template_get(tc_pipeline_template_handle handle) {
    if (!g_initialized || tc_pipeline_template_handle_is_invalid(handle)) return NULL;
    return (tc_pipeline_template*)tc_pool_get(&g_pool, handle);
}

bool tc_pipeline_template_is_valid(tc_pipeline_template_handle handle) {
    return g_initialized && tc_pool_is_valid(&g_pool, handle);
}

size_t tc_pipeline_template_count(void) {
    return g_initialized ? tc_pool_count(&g_pool) : 0;
}

static bool destroy_pipeline_template(tc_pipeline_template_handle handle) {
    tc_pipeline_template* pipeline_template = tc_pipeline_template_get(handle);
    if (!pipeline_template) return false;
    tc_resource_map_remove(g_uuid_to_index, pipeline_template->header.uuid);
    clear_payload(pipeline_template);
    return tc_pool_free_slot(&g_pool, handle);
}

void tc_pipeline_template_retain(tc_pipeline_template* pipeline_template) {
    if (pipeline_template) ++pipeline_template->header.ref_count;
}

bool tc_pipeline_template_release(tc_pipeline_template* pipeline_template) {
    if (!pipeline_template || pipeline_template->header.ref_count == 0) return false;
    --pipeline_template->header.ref_count;
    return pipeline_template->header.ref_count == 0 && destroy_pipeline_template(pipeline_template->self_handle);
}

bool tc_pipeline_template_remove(tc_pipeline_template_handle handle) {
    tc_pipeline_template* pipeline_template = tc_pipeline_template_get(handle);
    if (!pipeline_template) return false;
    if (pipeline_template->header.ref_count != 0) {
        tc_log_error(
            "tc_pipeline_template_remove: '%s' still has %u retained handle(s)",
            pipeline_template->header.uuid,
            pipeline_template->header.ref_count);
        return false;
    }
    return destroy_pipeline_template(handle);
}

static bool validate_payload(const tc_pipeline_template_payload_desc* desc) {
    if (!desc || desc->descriptor_version != TC_PIPELINE_TEMPLATE_DESCRIPTOR_VERSION
        || !desc->name || !desc->name[0]) {
        tc_log_error("tc_pipeline_template_set_payload: invalid descriptor version or name");
        return false;
    }
    if ((desc->pass_count && !desc->passes)
        || (desc->resource_count && !desc->resources)
        || (desc->dependency_count && !desc->dependencies)
        || (desc->target_count && !desc->targets)) {
        tc_log_error("tc_pipeline_template_set_payload: array pointer is NULL");
        return false;
    }
    for (uint32_t i = 0; i < desc->pass_count; ++i) {
        if (!desc->passes[i].type_name || !desc->passes[i].type_name[0]
            || !desc->passes[i].name || !desc->passes[i].name[0]) {
            tc_log_error("tc_pipeline_template_set_payload: pass %u lacks type or name", i);
            return false;
        }
    }
    for (uint32_t i = 0; i < desc->resource_count; ++i) {
        if (!desc->resources[i].name || !desc->resources[i].name[0]
            || !desc->resources[i].resource_type || !desc->resources[i].resource_type[0]) {
            tc_log_error("tc_pipeline_template_set_payload: resource %u lacks name or type", i);
            return false;
        }
    }
    for (uint32_t i = 0; i < desc->dependency_count; ++i) {
        if (desc->dependencies[i].pass_index >= desc->pass_count
            || !desc->dependencies[i].resource || !desc->dependencies[i].resource[0]
            || desc->dependencies[i].access < TC_PIPELINE_RESOURCE_READ
            || desc->dependencies[i].access > TC_PIPELINE_RESOURCE_READ_WRITE) {
            tc_log_error("tc_pipeline_template_set_payload: invalid dependency %u", i);
            return false;
        }
    }
    return true;
}

#define ALLOC_COPY(field, count) do { \
    if ((count) != 0) { \
        field = calloc((count), sizeof(*field)); \
        if (!field) goto allocation_failed; \
    } \
} while (0)

bool tc_pipeline_template_set_payload(
    tc_pipeline_template* pipeline_template,
    const tc_pipeline_template_payload_desc* desc
) {
    if (!pipeline_template || !validate_payload(desc)) return false;
    tc_pipeline_template_pass_desc* passes = NULL;
    tc_pipeline_template_resource_desc* resources = NULL;
    tc_pipeline_template_dependency_desc* dependencies = NULL;
    tc_pipeline_template_target_desc* targets = NULL;
    ALLOC_COPY(passes, desc->pass_count);
    ALLOC_COPY(resources, desc->resource_count);
    ALLOC_COPY(dependencies, desc->dependency_count);
    ALLOC_COPY(targets, desc->target_count);

    for (uint32_t i = 0; i < desc->pass_count; ++i) {
        passes[i] = desc->passes[i];
        passes[i].type_name = tc_intern_string(desc->passes[i].type_name);
        passes[i].name = tc_intern_string(desc->passes[i].name);
        passes[i].parameters = intern_optional(desc->passes[i].parameters);
        passes[i].viewport_name = intern_optional(desc->passes[i].viewport_name);
    }
    for (uint32_t i = 0; i < desc->resource_count; ++i) {
        resources[i] = desc->resources[i];
        resources[i].name = tc_intern_string(desc->resources[i].name);
        resources[i].resource_type = tc_intern_string(desc->resources[i].resource_type);
        resources[i].format = intern_optional(desc->resources[i].format);
        resources[i].viewport_name = intern_optional(desc->resources[i].viewport_name);
    }
    for (uint32_t i = 0; i < desc->dependency_count; ++i) {
        dependencies[i] = desc->dependencies[i];
        dependencies[i].resource = tc_intern_string(desc->dependencies[i].resource);
    }
    for (uint32_t i = 0; i < desc->target_count; ++i) {
        targets[i] = desc->targets[i];
        targets[i].viewport_name = intern_optional(desc->targets[i].viewport_name);
        targets[i].export_name = intern_optional(desc->targets[i].export_name);
    }

    clear_payload(pipeline_template);
    pipeline_template->header.name = tc_intern_string(desc->name);
    pipeline_template->descriptor_version = desc->descriptor_version;
    pipeline_template->passes = passes;
    pipeline_template->pass_count = desc->pass_count;
    pipeline_template->resources = resources;
    pipeline_template->resource_count = desc->resource_count;
    pipeline_template->dependencies = dependencies;
    pipeline_template->dependency_count = desc->dependency_count;
    pipeline_template->targets = targets;
    pipeline_template->target_count = desc->target_count;
    tc_resource_header_bump_version(&pipeline_template->header);
    return true;

allocation_failed:
    tc_log_error("tc_pipeline_template_set_payload: allocation failed");
    free(passes);
    free(resources);
    free(dependencies);
    free(targets);
    return false;
}

#undef ALLOC_COPY

uint32_t tc_pipeline_template_version(const tc_pipeline_template* pipeline_template) {
    return pipeline_template ? pipeline_template->header.version : 0;
}

typedef struct byte_writer {
    uint8_t* data;
    size_t capacity;
    size_t offset;
    bool valid;
} byte_writer;

static void write_bytes(byte_writer* writer, const void* value, size_t size) {
    if (writer->offset > SIZE_MAX - size) { writer->valid = false; return; }
    if (writer->data && writer->offset + size <= writer->capacity) {
        memcpy(writer->data + writer->offset, value, size);
    }
    writer->offset += size;
}

static void write_u32(byte_writer* writer, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value & 0xffu),
        (uint8_t)((value >> 8) & 0xffu),
        (uint8_t)((value >> 16) & 0xffu),
        (uint8_t)((value >> 24) & 0xffu),
    };
    write_bytes(writer, bytes, sizeof(bytes));
}

static void write_i32(byte_writer* writer, int32_t value) {
    write_u32(writer, (uint32_t)value);
}

static void write_f32(byte_writer* writer, float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    write_u32(writer, bits);
}

static void write_string(byte_writer* writer, const char* value) {
    uint32_t length = value ? (uint32_t)strlen(value) : 0;
    write_u32(writer, length);
    if (length) write_bytes(writer, value, length);
}

size_t tc_pipeline_template_serialize(
    const tc_pipeline_template* pipeline_template,
    uint8_t* output,
    size_t capacity
) {
    if (!pipeline_template) return 0;
    if (output) {
        const size_t required = tc_pipeline_template_serialize(pipeline_template, NULL, 0);
        if (capacity < required) return required;
    }
    byte_writer writer = {output, capacity, 0, true};
    const uint8_t magic[4] = {'T', 'P', 'L', 'T'};
    write_bytes(&writer, magic, sizeof(magic));
    write_u32(&writer, TC_PIPELINE_TEMPLATE_BINARY_VERSION);
    write_u32(&writer, pipeline_template->descriptor_version);
    write_string(&writer, pipeline_template->header.name);
    write_u32(&writer, pipeline_template->pass_count);
    write_u32(&writer, pipeline_template->resource_count);
    write_u32(&writer, pipeline_template->dependency_count);
    write_u32(&writer, pipeline_template->target_count);
    for (uint32_t i = 0; i < pipeline_template->pass_count; ++i) {
        write_string(&writer, pipeline_template->passes[i].type_name);
        write_string(&writer, pipeline_template->passes[i].name);
        write_string(&writer, pipeline_template->passes[i].parameters);
        write_string(&writer, pipeline_template->passes[i].viewport_name);
    }
    for (uint32_t i = 0; i < pipeline_template->resource_count; ++i) {
        const tc_pipeline_template_resource_desc* value = &pipeline_template->resources[i];
        write_string(&writer, value->name);
        write_string(&writer, value->resource_type);
        write_string(&writer, value->format);
        write_string(&writer, value->viewport_name);
        write_i32(&writer, value->width);
        write_i32(&writer, value->height);
        write_f32(&writer, value->scale);
        write_u32(&writer, value->samples);
        write_u32(&writer, value->flags);
    }
    for (uint32_t i = 0; i < pipeline_template->dependency_count; ++i) {
        write_u32(&writer, pipeline_template->dependencies[i].pass_index);
        write_string(&writer, pipeline_template->dependencies[i].resource);
        write_u32(&writer, (uint32_t)pipeline_template->dependencies[i].access);
    }
    for (uint32_t i = 0; i < pipeline_template->target_count; ++i) {
        write_string(&writer, pipeline_template->targets[i].viewport_name);
        write_string(&writer, pipeline_template->targets[i].export_name);
        write_i32(&writer, pipeline_template->targets[i].width);
        write_i32(&writer, pipeline_template->targets[i].height);
    }
    if (!writer.valid) return 0;
    if (output && writer.offset > capacity) return writer.offset;
    return writer.offset;
}

typedef struct byte_reader {
    const uint8_t* data;
    size_t size;
    size_t offset;
    bool valid;
} byte_reader;

static void read_bytes(byte_reader* reader, void* output, size_t size) {
    if (!reader->valid || reader->offset > reader->size || size > reader->size - reader->offset) {
        reader->valid = false;
        return;
    }
    memcpy(output, reader->data + reader->offset, size);
    reader->offset += size;
}

static uint32_t read_u32(byte_reader* reader) {
    uint8_t bytes[4] = {0};
    read_bytes(reader, bytes, sizeof(bytes));
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
}

static int32_t read_i32(byte_reader* reader) {
    return (int32_t)read_u32(reader);
}

static float read_f32(byte_reader* reader) {
    uint32_t bits = read_u32(reader);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static char* read_string(byte_reader* reader) {
    uint32_t length = read_u32(reader);
    if (!reader->valid || length > reader->size - reader->offset) {
        reader->valid = false;
        return NULL;
    }
    char* value = (char*)malloc((size_t)length + 1);
    if (!value) { reader->valid = false; return NULL; }
    if (length) read_bytes(reader, value, length);
    value[length] = '\0';
    return value;
}

static void free_decoded_payload(tc_pipeline_template_payload_desc* desc) {
    if (!desc) return;
    free((void*)desc->name);
    for (uint32_t i = 0; desc->passes && i < desc->pass_count; ++i) {
        free((void*)desc->passes[i].type_name);
        free((void*)desc->passes[i].name);
        free((void*)desc->passes[i].parameters);
        free((void*)desc->passes[i].viewport_name);
    }
    for (uint32_t i = 0; desc->resources && i < desc->resource_count; ++i) {
        free((void*)desc->resources[i].name);
        free((void*)desc->resources[i].resource_type);
        free((void*)desc->resources[i].format);
        free((void*)desc->resources[i].viewport_name);
    }
    for (uint32_t i = 0; desc->dependencies && i < desc->dependency_count; ++i) {
        free((void*)desc->dependencies[i].resource);
    }
    for (uint32_t i = 0; desc->targets && i < desc->target_count; ++i) {
        free((void*)desc->targets[i].viewport_name);
        free((void*)desc->targets[i].export_name);
    }
    free((void*)desc->passes);
    free((void*)desc->resources);
    free((void*)desc->dependencies);
    free((void*)desc->targets);
}

tc_pipeline_template_handle tc_pipeline_template_deserialize(
    const char* uuid,
    const uint8_t* data,
    size_t size
) {
    tc_pipeline_template_payload_desc desc;
    memset(&desc, 0, sizeof(desc));
    if (!uuid || !uuid[0] || !data) {
        tc_log_error("tc_pipeline_template_deserialize: UUID and data are required");
        return tc_pipeline_template_handle_invalid();
    }
    byte_reader reader = {data, size, 0, true};
    uint8_t magic[4] = {0};
    read_bytes(&reader, magic, sizeof(magic));
    uint32_t binary_version = read_u32(&reader);
    desc.descriptor_version = read_u32(&reader);
    desc.name = read_string(&reader);
    desc.pass_count = read_u32(&reader);
    desc.resource_count = read_u32(&reader);
    desc.dependency_count = read_u32(&reader);
    desc.target_count = read_u32(&reader);
    if (!reader.valid || memcmp(magic, "TPLT", 4) != 0
        || binary_version != TC_PIPELINE_TEMPLATE_BINARY_VERSION
        || desc.descriptor_version != TC_PIPELINE_TEMPLATE_DESCRIPTOR_VERSION
        || desc.pass_count > 65536 || desc.resource_count > 65536
        || desc.dependency_count > 262144 || desc.target_count > 65536) {
        tc_log_error("tc_pipeline_template_deserialize: invalid header");
        free_decoded_payload(&desc);
        return tc_pipeline_template_handle_invalid();
    }
    desc.passes = (tc_pipeline_template_pass_desc*)calloc(desc.pass_count, sizeof(*desc.passes));
    desc.resources = (tc_pipeline_template_resource_desc*)calloc(desc.resource_count, sizeof(*desc.resources));
    desc.dependencies = (tc_pipeline_template_dependency_desc*)calloc(desc.dependency_count, sizeof(*desc.dependencies));
    desc.targets = (tc_pipeline_template_target_desc*)calloc(desc.target_count, sizeof(*desc.targets));
    if ((desc.pass_count && !desc.passes) || (desc.resource_count && !desc.resources)
        || (desc.dependency_count && !desc.dependencies) || (desc.target_count && !desc.targets)) {
        reader.valid = false;
    }
    for (uint32_t i = 0; reader.valid && i < desc.pass_count; ++i) {
        tc_pipeline_template_pass_desc* value = (tc_pipeline_template_pass_desc*)&desc.passes[i];
        value->type_name = read_string(&reader);
        value->name = read_string(&reader);
        value->parameters = read_string(&reader);
        value->viewport_name = read_string(&reader);
    }
    for (uint32_t i = 0; reader.valid && i < desc.resource_count; ++i) {
        tc_pipeline_template_resource_desc* value = (tc_pipeline_template_resource_desc*)&desc.resources[i];
        value->name = read_string(&reader);
        value->resource_type = read_string(&reader);
        value->format = read_string(&reader);
        value->viewport_name = read_string(&reader);
        value->width = read_i32(&reader);
        value->height = read_i32(&reader);
        value->scale = read_f32(&reader);
        value->samples = read_u32(&reader);
        value->flags = read_u32(&reader);
    }
    for (uint32_t i = 0; reader.valid && i < desc.dependency_count; ++i) {
        tc_pipeline_template_dependency_desc* value = (tc_pipeline_template_dependency_desc*)&desc.dependencies[i];
        value->pass_index = read_u32(&reader);
        value->resource = read_string(&reader);
        value->access = (tc_pipeline_resource_access)read_u32(&reader);
    }
    for (uint32_t i = 0; reader.valid && i < desc.target_count; ++i) {
        tc_pipeline_template_target_desc* value = (tc_pipeline_template_target_desc*)&desc.targets[i];
        value->viewport_name = read_string(&reader);
        value->export_name = read_string(&reader);
        value->width = read_i32(&reader);
        value->height = read_i32(&reader);
    }
    if (!reader.valid || reader.offset != reader.size) {
        tc_log_error("tc_pipeline_template_deserialize: truncated or trailing data");
        free_decoded_payload(&desc);
        return tc_pipeline_template_handle_invalid();
    }
    tc_pipeline_template_handle handle = tc_pipeline_template_create(uuid, desc.name);
    tc_pipeline_template* pipeline_template = tc_pipeline_template_get(handle);
    if (!pipeline_template || !tc_pipeline_template_set_payload(pipeline_template, &desc)) {
        if (pipeline_template) tc_pipeline_template_remove(handle);
        handle = tc_pipeline_template_handle_invalid();
    }
    free_decoded_payload(&desc);
    return handle;
}
