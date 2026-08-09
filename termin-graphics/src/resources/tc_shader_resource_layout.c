#include "tc_shader_resource_layout_internal.h"

#include <tcbase/tc_log.h>

#include <stdlib.h>
#include <string.h>

static int tc_shader_resource_binding_compare(const void* a, const void* b) {
    const tc_shader_resource_binding* ra = (const tc_shader_resource_binding*)a;
    const tc_shader_resource_binding* rb = (const tc_shader_resource_binding*)b;
    return strcmp(ra->name, rb->name);
}

int tc_shader_resource_requirement_compare(const void* a, const void* b) {
    const tc_shader_resource_requirement* ra = (const tc_shader_resource_requirement*)a;
    const tc_shader_resource_requirement* rb = (const tc_shader_resource_requirement*)b;
    return strcmp(ra->name, rb->name);
}

static int tc_shader_find_resource_binding_index(const tc_shader* shader, const char* name) {
    if (!shader || !name || name[0] == '\0')
        return -1;
    for (uint32_t i = 0; i < shader->resource_binding_count; i++) {
        if (strcmp(shader->resource_bindings[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int tc_shader_find_resource_binding_index_sorted(const tc_shader* shader, const char* name) {
    if (!shader || !name || name[0] == '\0' || shader->resource_binding_count == 0) {
        return -1;
    }
    uint32_t lo = 0;
    uint32_t hi = shader->resource_binding_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(shader->resource_bindings[mid].name, name);
        if (cmp < 0) {
            lo = mid + 1;
        } else if (cmp > 0) {
            hi = mid;
        } else {
            return (int)mid;
        }
    }
    return -1;
}

static bool tc_shader_upsert_resource_binding(tc_shader* shader, const tc_shader_resource_binding* binding) {
    if (!shader || !binding || binding->name[0] == '\0')
        return false;

    int existing = tc_shader_find_resource_binding_index(shader, binding->name);
    if (existing >= 0) {
        tc_shader_resource_binding previous = shader->resource_bindings[existing];
        shader->resource_bindings[existing] = *binding;
        shader->resource_bindings[existing].name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
        if (!binding->has_d3d11_placement && previous.has_d3d11_placement) {
            shader->resource_bindings[existing].has_d3d11_placement = 1;
            shader->resource_bindings[existing].d3d11 = previous.d3d11;
        }
        if (!binding->has_webgpu_placement && previous.has_webgpu_placement) {
            shader->resource_bindings[existing].has_webgpu_placement = 1;
            shader->resource_bindings[existing].webgpu = previous.webgpu;
        }
        if (binding->fields == NULL && previous.fields != NULL) {
            shader->resource_bindings[existing].fields = previous.fields;
            shader->resource_bindings[existing].field_count = previous.field_count;
        } else if (previous.fields != NULL && previous.fields != binding->fields) {
            free(previous.fields);
        }
        return true;
    }

    uint32_t new_count = shader->resource_binding_count + 1u;
    size_t bytes = (size_t)new_count * sizeof(tc_shader_resource_binding);
    tc_shader_resource_binding* copy = (tc_shader_resource_binding*)realloc(shader->resource_bindings, bytes);
    if (!copy) {
        tc_log(TC_LOG_ERROR, "tc_shader_upsert_resource_binding: allocation failed (%u entries)", new_count);
        return false;
    }
    shader->resource_bindings = copy;
    shader->resource_bindings[shader->resource_binding_count] = *binding;
    shader->resource_bindings[shader->resource_binding_count].name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
    shader->resource_binding_count = new_count;
    return true;
}

static bool tc_shader_material_ubo_entry_valid(const tc_material_ubo_entry* entry) {
    if (!entry || entry->name[0] == '\0' || entry->property_type[0] == '\0' ||
        !memchr(entry->name, '\0', sizeof(entry->name)) ||
        !memchr(entry->property_type, '\0', sizeof(entry->property_type))) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_material_ubo_layout: entry requires name and property type");
        return false;
    }

    uint32_t expected_size = 0;
    if (strcmp(entry->property_type, "Float") == 0 || strcmp(entry->property_type, "Int") == 0 ||
        strcmp(entry->property_type, "Bool") == 0) {
        expected_size = 4;
    } else if (strcmp(entry->property_type, "Vec2") == 0) {
        expected_size = 8;
    } else if (strcmp(entry->property_type, "Vec3") == 0) {
        expected_size = 12;
    } else if (strcmp(entry->property_type, "Vec4") == 0 ||
               strcmp(entry->property_type, "SrgbColor") == 0 ||
               strcmp(entry->property_type, "LinearColor") == 0) {
        expected_size = 16;
    } else if (strcmp(entry->property_type, "Mat4") == 0) {
        expected_size = 64;
    } else {
        tc_log(TC_LOG_ERROR,
               "tc_shader_set_material_ubo_layout: unsupported property type '%s' for '%s'",
               entry->property_type,
               entry->name);
        return false;
    }

    if (entry->size != expected_size) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_set_material_ubo_layout: property '%s' type '%s' has size %u, expected %u",
               entry->name,
               entry->property_type,
               entry->size,
               expected_size);
        return false;
    }
    return true;
}

void tc_shader_set_material_ubo_layout(tc_shader* shader,
                                       const tc_material_ubo_entry* entries,
                                       uint32_t count,
                                       uint32_t block_size) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "[Stage 5.H bridge] set_material_ubo_layout called with NULL shader");
        return;
    }

    if (count == 0 || !entries) {
        // An empty descriptor explicitly clears the current layout.
        if (shader->material_ubo_entries) {
            free(shader->material_ubo_entries);
            shader->material_ubo_entries = NULL;
        }
        shader->material_ubo_entry_count = 0;
        shader->material_ubo_block_size = 0;
        return;
    }

    // Validate before replacing the current layout. A malformed descriptor is
    // rejected atomically, leaving the last valid reflection available.
    for (uint32_t i = 0; i < count; ++i) {
        if (!tc_shader_material_ubo_entry_valid(&entries[i])) {
            tc_log(TC_LOG_ERROR, "tc_shader_set_material_ubo_layout: rejecting invalid entry %u", i);
            return;
        }
    }

    size_t bytes = (size_t)count * sizeof(tc_material_ubo_entry);
    tc_material_ubo_entry* copy = (tc_material_ubo_entry*)malloc(bytes);
    if (!copy) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_material_ubo_layout: allocation failed (%u entries)", count);
        return;
    }
    memcpy(copy, entries, bytes);

    // Allocate first so replacement is atomic even under allocation failure.
    if (shader->material_ubo_entries) {
        free(shader->material_ubo_entries);
    }
    shader->material_ubo_entries = copy;
    shader->material_ubo_entry_count = count;
    shader->material_ubo_block_size = block_size;
}

uint32_t tc_shader_material_ubo_entry_count(const tc_shader* shader) {
    return shader ? shader->material_ubo_entry_count : 0u;
}

const tc_material_ubo_entry* tc_shader_material_ubo_entries(const tc_shader* shader) {
    return shader ? shader->material_ubo_entries : NULL;
}

uint32_t tc_shader_material_ubo_block_size(const tc_shader* shader) {
    return shader ? shader->material_ubo_block_size : 0u;
}

// ============================================================================
// Shader resource layout
// ============================================================================

static void tc_shader_free_resource_binding_array(tc_shader_resource_binding* bindings, uint32_t count) {
    if (!bindings)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        free(bindings[i].fields);
        bindings[i].fields = NULL;
        bindings[i].field_count = 0;
    }
    free(bindings);
}

void tc_shader_free_resource_requirement_array(tc_shader_resource_requirement* requirements, uint32_t count) {
    if (!requirements)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        free(requirements[i].fields);
        requirements[i].fields = NULL;
        requirements[i].field_count = 0;
    }
    free(requirements);
}

static bool tc_shader_validate_resource_layout(const tc_shader_resource_binding* bindings, uint32_t count) {
    if (!bindings || count == 0)
        return true;
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = i + 1u; j < count; ++j) {
            const tc_shader_resource_binding* a = &bindings[i];
            const tc_shader_resource_binding* b = &bindings[j];
            if (a->has_webgpu_placement && b->has_webgpu_placement) {
                const bool same_primary = a->webgpu.group == b->webgpu.group && a->webgpu.binding == b->webgpu.binding;
                const bool a_sampler_hits_b = a->webgpu.has_sampler_binding && a->webgpu.group == b->webgpu.group &&
                                              a->webgpu.sampler_binding == b->webgpu.binding;
                const bool b_sampler_hits_a = b->webgpu.has_sampler_binding && a->webgpu.group == b->webgpu.group &&
                                              b->webgpu.sampler_binding == a->webgpu.binding;
                const bool sampler_collision = a->webgpu.has_sampler_binding && b->webgpu.has_sampler_binding &&
                                               a->webgpu.group == b->webgpu.group &&
                                               a->webgpu.sampler_binding == b->webgpu.sampler_binding;
                if (same_primary || a_sampler_hits_b || b_sampler_hits_a || sampler_collision) {
                    tc_log(TC_LOG_ERROR,
                           "tc_shader_set_resource_layout: conflicting WebGPU resources in group=%u: '%s' vs '%s'",
                           a->webgpu.group,
                           a->name,
                           b->name);
                    return false;
                }
            }
            if (!a->has_d3d11_placement || !b->has_d3d11_placement) {
                continue;
            }
            if (a->d3d11.register_class != b->d3d11.register_class ||
                a->d3d11.register_index != b->d3d11.register_index || (a->stage_mask & b->stage_mask) == 0u) {
                continue;
            }
            if (strncmp(a->name, b->name, TC_SHADER_RESOURCE_NAME_MAX) == 0 && a->kind == b->kind &&
                a->scope == b->scope) {
                continue;
            }
            tc_log(TC_LOG_ERROR,
                   "tc_shader_set_resource_layout: conflicting D3D11 resources at class=%u register=%u "
                   "stage_mask=0x%x: '%s' kind=%u scope=%u vs '%s' kind=%u scope=%u",
                   a->d3d11.register_class,
                   a->d3d11.register_index,
                   a->stage_mask & b->stage_mask,
                   a->name,
                   a->kind,
                   a->scope,
                   b->name,
                   b->kind,
                   b->scope);
            return false;
        }
    }
    return true;
}

void tc_shader_set_resource_layout(tc_shader* shader, const tc_shader_resource_binding* bindings, uint32_t count) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_resource_layout called with NULL shader");
        return;
    }

    if (count == 0 || !bindings) {
        if (shader->resource_bindings) {
            tc_shader_free_resource_binding_array(shader->resource_bindings, shader->resource_binding_count);
            shader->resource_bindings = NULL;
        }
        shader->resource_binding_count = 0;
        shader->has_resource_layout = 0;
        return;
    }

    tc_shader_resource_binding* copy = (tc_shader_resource_binding*)calloc(count, sizeof(tc_shader_resource_binding));
    if (!copy) {
        tc_log(TC_LOG_ERROR, "tc_shader_set_resource_layout: allocation failed (%u entries)", count);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        copy[i] = bindings[i];
        copy[i].name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
        copy[i].fields = NULL;
        if (bindings[i].field_count > 0 && bindings[i].fields) {
            size_t field_bytes = (size_t)bindings[i].field_count * sizeof(tc_shader_resource_field);
            copy[i].fields = (tc_shader_resource_field*)malloc(field_bytes);
            if (!copy[i].fields) {
                tc_log(TC_LOG_ERROR,
                       "tc_shader_set_resource_layout: field allocation failed for '%s' (%u fields)",
                       copy[i].name,
                       bindings[i].field_count);
                tc_shader_free_resource_binding_array(copy, count);
                return;
            }
            memcpy(copy[i].fields, bindings[i].fields, field_bytes);
            for (uint32_t f = 0; f < bindings[i].field_count; ++f) {
                copy[i].fields[f].name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
            }
        } else {
            copy[i].field_count = 0;
        }
    }

    if (!tc_shader_validate_resource_layout(copy, count)) {
        tc_shader_free_resource_binding_array(copy, count);
        return;
    }

    if (count > 1) {
        qsort(copy, count, sizeof(tc_shader_resource_binding), tc_shader_resource_binding_compare);
    }

    if (shader->resource_bindings) {
        tc_shader_free_resource_binding_array(shader->resource_bindings, shader->resource_binding_count);
        shader->resource_bindings = NULL;
    }
    shader->resource_binding_count = 0;

    shader->resource_bindings = copy;
    shader->resource_binding_count = count;
    shader->has_resource_layout = 1;
}

uint32_t tc_shader_resource_binding_count(const tc_shader* shader) {
    return shader ? shader->resource_binding_count : 0u;
}

const tc_shader_resource_binding* tc_shader_resource_bindings(const tc_shader* shader) {
    return shader ? shader->resource_bindings : NULL;
}

const tc_shader_resource_binding* tc_shader_find_resource_binding(const tc_shader* shader, const char* name) {
    int index = tc_shader_find_resource_binding_index_sorted(shader, name);
    if (index < 0)
        return NULL;
    return &shader->resource_bindings[index];
}

bool tc_shader_has_resource_layout(const tc_shader* shader) {
    return shader && shader->has_resource_layout != 0;
}

void tc_shader_mark_resource_layout_known(tc_shader* shader) {
    if (shader) {
        shader->has_resource_layout = 1;
    }
}
