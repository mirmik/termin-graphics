// tc_shader_abi.h - Termin shader ABI resource vocabulary
#pragma once

#include "tgfx/tgfx_api.h"
#include "tgfx/resources/tc_shader.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tc_shader_abi_resource_id {
    TC_SHADER_ABI_RESOURCE_PER_FRAME = 0,
    TC_SHADER_ABI_RESOURCE_DRAW_DATA = 1,
    TC_SHADER_ABI_RESOURCE_MATERIAL = 2,
    TC_SHADER_ABI_RESOURCE_BONE_BLOCK = 3,
    TC_SHADER_ABI_RESOURCE_LIGHTING = 4,
    TC_SHADER_ABI_RESOURCE_SHADOW_BLOCK = 5,
    TC_SHADER_ABI_RESOURCE_SHADOW_MAPS = 6,
} tc_shader_abi_resource_id;

typedef struct tc_shader_abi_resource_decl {
    uint32_t id;              // tc_shader_abi_resource_id
    const char* canonical_name;
    uint32_t kind;            // tc_shader_resource_kind
    uint32_t scope;           // tc_shader_resource_scope
    const char* const* legacy_aliases;
    uint32_t legacy_alias_count;
} tc_shader_abi_resource_decl;

TGFX_API const tc_shader_abi_resource_decl* tc_shader_abi_resource(uint32_t id);
TGFX_API const tc_shader_abi_resource_decl* tc_shader_abi_find_resource(const char* name);
TGFX_API bool tc_shader_abi_name_matches(
    const tc_shader_abi_resource_decl* decl,
    const char* name);
TGFX_API bool tc_shader_abi_name_is_legacy_alias(
    const tc_shader_abi_resource_decl* decl,
    const char* name);
TGFX_API bool tc_shader_abi_binding_matches(
    const tc_shader_abi_resource_decl* decl,
    const tc_shader_resource_binding* binding);
TGFX_API const tc_shader_resource_binding* tc_shader_abi_find_resource_binding(
    const tc_shader* shader,
    uint32_t id);

#ifdef __cplusplus
}
#endif
