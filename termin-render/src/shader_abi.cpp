#include "termin/render/shader_abi.hpp"

#include <array>
#include <cstdlib>

#include "tcbase/tc_log.hpp"

extern "C" {
#include "tgfx/resources/tc_shader_registry.h"
}

namespace termin {
namespace {

constexpr std::array<std::string_view, 1> kDrawDataAliases = {
    "draw",
};

constexpr std::array<std::string_view, 2> kLightingAliases = {
    "lighting_ubo",
    "LightingBlock",
};

constexpr std::array<std::string_view, 1> kShadowBlockAliases = {
    "ShadowBlock",
};

constexpr std::array<std::string_view, 1> kShadowMapsAliases = {
    "u_shadow_map",
};

constexpr std::array<ShaderAbiResourceDecl, 7> kShaderAbiResources = {{
    {
        ShaderAbiResourceId::PerFrame,
        TC_SHADER_RESOURCE_PER_FRAME,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_FRAME,
        {},
    },
    {
        ShaderAbiResourceId::DrawData,
        TC_SHADER_RESOURCE_DRAW_DATA,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        kDrawDataAliases,
    },
    {
        ShaderAbiResourceId::MaterialParams,
        TC_SHADER_RESOURCE_MATERIAL,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        {},
    },
    {
        ShaderAbiResourceId::BoneBlock,
        TC_SHADER_RESOURCE_BONE_BLOCK,
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        {},
    },
    {
        ShaderAbiResourceId::Lighting,
        "lighting",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_PASS,
        kLightingAliases,
    },
    {
        ShaderAbiResourceId::ShadowBlock,
        "shadow_block",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_PASS,
        kShadowBlockAliases,
    },
    {
        ShaderAbiResourceId::ShadowMaps,
        "shadow_maps",
        TC_SHADER_RESOURCE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_PASS,
        kShadowMapsAliases,
    },
}};

const char* shader_debug_name(const tc_shader* shader) {
    if (!shader) {
        return "<null>";
    }
    if (shader->name && shader->name[0] != '\0') {
        return shader->name;
    }
    if (shader->uuid[0] != '\0') {
        return shader->uuid;
    }
    return "<unnamed>";
}

} // namespace

const ShaderAbiResourceDecl& shader_abi_resource(ShaderAbiResourceId id) {
    for (const ShaderAbiResourceDecl& decl : kShaderAbiResources) {
        if (decl.id == id) {
            return decl;
        }
    }
    tc::Log::error(
        "[ShaderABI] unknown shader ABI resource id %u",
        static_cast<unsigned>(id));
    std::abort();
}

bool shader_abi_name_is_legacy_alias(
    const ShaderAbiResourceDecl& decl,
    std::string_view name)
{
    for (std::string_view alias : decl.legacy_aliases) {
        if (alias == name) {
            return true;
        }
    }
    return false;
}

bool shader_abi_name_matches(
    const ShaderAbiResourceDecl& decl,
    std::string_view name)
{
    return decl.canonical_name == name ||
        shader_abi_name_is_legacy_alias(decl, name);
}

const ShaderAbiResourceDecl* find_shader_abi_resource(std::string_view name) {
    for (const ShaderAbiResourceDecl& decl : kShaderAbiResources) {
        if (shader_abi_name_matches(decl, name)) {
            return &decl;
        }
    }
    return nullptr;
}

bool shader_abi_binding_matches(
    const ShaderAbiResourceDecl& decl,
    const tc_shader_resource_binding& binding)
{
    return shader_abi_name_matches(decl, binding.name) &&
        binding.kind == decl.kind &&
        binding.scope == decl.scope;
}

const tc_shader_resource_binding* find_shader_abi_resource_binding(
    const tc_shader* shader,
    ShaderAbiResourceId id)
{
    if (!shader) {
        return nullptr;
    }
    const ShaderAbiResourceDecl& decl = shader_abi_resource(id);
    const tc_shader_resource_binding* rb =
        tc_shader_find_resource_binding(shader, decl.canonical_name.data());
    if (rb) {
        if (shader_abi_binding_matches(decl, *rb)) {
            return rb;
        }
        tc::Log::error(
            "[ShaderABI] shader '%s' declares ABI resource '%s' with "
            "kind=%u scope=%u, expected kind=%u scope=%u",
            shader_debug_name(shader),
            rb->name,
            rb->kind,
            rb->scope,
            decl.kind,
            decl.scope);
        return nullptr;
    }
    for (std::string_view alias : decl.legacy_aliases) {
        rb = tc_shader_find_resource_binding(shader, alias.data());
        if (rb) {
            if (shader_abi_binding_matches(decl, *rb)) {
                return rb;
            }
            tc::Log::error(
                "[ShaderABI] shader '%s' declares ABI resource '%s' with "
                "kind=%u scope=%u, expected kind=%u scope=%u",
                shader_debug_name(shader),
                rb->name,
                rb->kind,
                rb->scope,
                decl.kind,
                decl.scope);
            return nullptr;
        }
    }
    return nullptr;
}

} // namespace termin
