#include "termin/render/shader_abi.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "tcbase/tc_log.hpp"

extern "C" {
#include "tgfx/resources/tc_shader_abi.h"
#include "tgfx/resources/tc_shader_registry.h"
}

namespace termin {
namespace {

constexpr uint32_t shader_abi_c_id(ShaderAbiResourceId id) {
    switch (id) {
    case ShaderAbiResourceId::PerFrame:
        return TC_SHADER_ABI_RESOURCE_PER_FRAME;
    case ShaderAbiResourceId::DrawData:
        return TC_SHADER_ABI_RESOURCE_DRAW_DATA;
    case ShaderAbiResourceId::MaterialParams:
        return TC_SHADER_ABI_RESOURCE_MATERIAL;
    case ShaderAbiResourceId::BoneBlock:
        return TC_SHADER_ABI_RESOURCE_BONE_BLOCK;
    case ShaderAbiResourceId::Lighting:
        return TC_SHADER_ABI_RESOURCE_LIGHTING;
    case ShaderAbiResourceId::ShadowBlock:
        return TC_SHADER_ABI_RESOURCE_SHADOW_BLOCK;
    case ShaderAbiResourceId::ShadowMaps:
        return TC_SHADER_ABI_RESOURCE_SHADOW_MAPS;
    }
    return UINT32_MAX;
}

ShaderAbiResourceDecl make_cpp_decl(
    const tc_shader_abi_resource_decl& decl)
{
    return {
        static_cast<ShaderAbiResourceId>(decl.id),
        decl.canonical_name,
        decl.kind,
        decl.scope,
        std::span<const char* const>(decl.legacy_aliases, decl.legacy_alias_count),
    };
}

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
    static ShaderAbiResourceDecl resources[] = {
        make_cpp_decl(*tc_shader_abi_resource(TC_SHADER_ABI_RESOURCE_PER_FRAME)),
        make_cpp_decl(*tc_shader_abi_resource(TC_SHADER_ABI_RESOURCE_DRAW_DATA)),
        make_cpp_decl(*tc_shader_abi_resource(TC_SHADER_ABI_RESOURCE_MATERIAL)),
        make_cpp_decl(*tc_shader_abi_resource(TC_SHADER_ABI_RESOURCE_BONE_BLOCK)),
        make_cpp_decl(*tc_shader_abi_resource(TC_SHADER_ABI_RESOURCE_LIGHTING)),
        make_cpp_decl(*tc_shader_abi_resource(TC_SHADER_ABI_RESOURCE_SHADOW_BLOCK)),
        make_cpp_decl(*tc_shader_abi_resource(TC_SHADER_ABI_RESOURCE_SHADOW_MAPS)),
    };

    const uint32_t c_id = shader_abi_c_id(id);
    const tc_shader_abi_resource_decl* c_decl = tc_shader_abi_resource(c_id);
    if (c_decl && c_decl->id < (sizeof(resources) / sizeof(resources[0]))) {
        return resources[c_decl->id];
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
    const tc_shader_abi_resource_decl* c_decl =
        tc_shader_abi_find_resource(std::string(name).c_str());
    if (!c_decl) {
        return nullptr;
    }
    return &shader_abi_resource(static_cast<ShaderAbiResourceId>(c_decl->id));
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
        tc_shader_abi_find_resource_binding(shader, shader_abi_c_id(id));
    if (rb) {
        return rb;
    }
    const tc_shader_abi_resource_decl* c_decl =
        tc_shader_abi_resource(shader_abi_c_id(id));
    if (c_decl && tc_shader_abi_find_resource_binding(shader, c_decl->id) == nullptr) {
        const tc_shader_resource_binding* invalid =
            tc_shader_find_resource_binding(shader, c_decl->canonical_name);
        for (uint32_t i = 0; !invalid && i < c_decl->legacy_alias_count; ++i) {
            invalid = tc_shader_find_resource_binding(shader, c_decl->legacy_aliases[i]);
        }
        if (invalid) {
            tc::Log::error(
                "[ShaderABI] shader '%s' declares ABI resource '%s' with "
                "kind=%u scope=%u, expected kind=%u scope=%u",
                shader_debug_name(shader),
                invalid->name,
                invalid->kind,
                invalid->scope,
                decl.kind,
                decl.scope);
        }
    }
    return nullptr;
}

} // namespace termin
