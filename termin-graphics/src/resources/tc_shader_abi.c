#include "tgfx/resources/tc_shader_abi.h"

#include "tgfx/resources/tc_shader_registry.h"

#include <string.h>

static const char* const k_draw_data_aliases[] = {
    TC_SHADER_RESOURCE_DRAW,
};

static const char* const k_bone_block_aliases[] = {
    "BoneBlock",
};

static const char* const k_lighting_aliases[] = {
    "lighting_ubo",
    "LightingBlock",
};

static const char* const k_shadow_block_aliases[] = {
    "ShadowBlock",
};

static const char* const k_shadow_maps_aliases[] = {
    "u_shadow_map",
};

static const tc_shader_abi_resource_decl k_shader_abi_resources[] = {
    {
        TC_SHADER_ABI_RESOURCE_PER_FRAME,
        TC_SHADER_RESOURCE_PER_FRAME,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_FRAME,
        NULL,
        0,
    },
    {
        TC_SHADER_ABI_RESOURCE_DRAW_DATA,
        TC_SHADER_RESOURCE_DRAW_DATA,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        k_draw_data_aliases,
        sizeof(k_draw_data_aliases) / sizeof(k_draw_data_aliases[0]),
    },
    {
        TC_SHADER_ABI_RESOURCE_MATERIAL,
        TC_SHADER_RESOURCE_MATERIAL,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        NULL,
        0,
    },
    {
        TC_SHADER_ABI_RESOURCE_BONE_BLOCK,
        TC_SHADER_RESOURCE_BONE_BLOCK,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        k_bone_block_aliases,
        sizeof(k_bone_block_aliases) / sizeof(k_bone_block_aliases[0]),
    },
    {
        TC_SHADER_ABI_RESOURCE_LIGHTING,
        "lighting",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_PASS,
        k_lighting_aliases,
        sizeof(k_lighting_aliases) / sizeof(k_lighting_aliases[0]),
    },
    {
        TC_SHADER_ABI_RESOURCE_SHADOW_BLOCK,
        "shadow_block",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_PASS,
        k_shadow_block_aliases,
        sizeof(k_shadow_block_aliases) / sizeof(k_shadow_block_aliases[0]),
    },
    {
        TC_SHADER_ABI_RESOURCE_SHADOW_MAPS,
        "shadow_maps",
        TC_SHADER_RESOURCE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_PASS,
        k_shadow_maps_aliases,
        sizeof(k_shadow_maps_aliases) / sizeof(k_shadow_maps_aliases[0]),
    },
};

static uint32_t shader_abi_resource_count(void) {
    return (uint32_t)(sizeof(k_shader_abi_resources) / sizeof(k_shader_abi_resources[0]));
}

const tc_shader_abi_resource_decl* tc_shader_abi_resource(uint32_t id) {
    for (uint32_t i = 0; i < shader_abi_resource_count(); ++i) {
        if (k_shader_abi_resources[i].id == id) {
            return &k_shader_abi_resources[i];
        }
    }
    return NULL;
}

bool tc_shader_abi_name_is_legacy_alias(
    const tc_shader_abi_resource_decl* decl,
    const char* name)
{
    if (!decl || !name) {
        return false;
    }
    for (uint32_t i = 0; i < decl->legacy_alias_count; ++i) {
        if (decl->legacy_aliases[i] && strcmp(decl->legacy_aliases[i], name) == 0) {
            return true;
        }
    }
    return false;
}

bool tc_shader_abi_name_matches(
    const tc_shader_abi_resource_decl* decl,
    const char* name)
{
    if (!decl || !decl->canonical_name || !name) {
        return false;
    }
    return strcmp(decl->canonical_name, name) == 0 ||
        tc_shader_abi_name_is_legacy_alias(decl, name);
}

const tc_shader_abi_resource_decl* tc_shader_abi_find_resource(const char* name) {
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0; i < shader_abi_resource_count(); ++i) {
        if (tc_shader_abi_name_matches(&k_shader_abi_resources[i], name)) {
            return &k_shader_abi_resources[i];
        }
    }
    return NULL;
}

bool tc_shader_abi_binding_matches(
    const tc_shader_abi_resource_decl* decl,
    const tc_shader_resource_binding* binding)
{
    if (!decl || !binding) {
        return false;
    }
    return tc_shader_abi_name_matches(decl, binding->name) &&
        binding->kind == decl->kind &&
        binding->scope == decl->scope;
}

const tc_shader_resource_binding* tc_shader_abi_find_resource_binding(
    const tc_shader* shader,
    uint32_t id)
{
    if (!shader) {
        return NULL;
    }
    const tc_shader_abi_resource_decl* decl = tc_shader_abi_resource(id);
    if (!decl) {
        return NULL;
    }
    const tc_shader_resource_binding* rb =
        tc_shader_find_resource_binding(shader, decl->canonical_name);
    if (rb && tc_shader_abi_binding_matches(decl, rb)) {
        return rb;
    }
    for (uint32_t i = 0; i < decl->legacy_alias_count; ++i) {
        rb = tc_shader_find_resource_binding(shader, decl->legacy_aliases[i]);
        if (rb && tc_shader_abi_binding_matches(decl, rb)) {
            return rb;
        }
    }
    return NULL;
}
