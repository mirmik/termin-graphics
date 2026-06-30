#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <termin/render/render_export.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

namespace termin {

enum class ShaderAbiResourceId : uint8_t {
    PerFrame,
    DrawData,
    MaterialParams,
    BoneBlock,
    Lighting,
    ShadowBlock,
    ShadowMaps,
};

struct ShaderAbiResourceDecl {
    ShaderAbiResourceId id;
    std::string_view canonical_name;
    uint32_t kind;
    uint32_t scope;
    std::span<const std::string_view> legacy_aliases;
};

RENDER_API const ShaderAbiResourceDecl& shader_abi_resource(
    ShaderAbiResourceId id);

RENDER_API const ShaderAbiResourceDecl* find_shader_abi_resource(
    std::string_view name);

RENDER_API bool shader_abi_name_matches(
    const ShaderAbiResourceDecl& decl,
    std::string_view name);

RENDER_API bool shader_abi_name_is_legacy_alias(
    const ShaderAbiResourceDecl& decl,
    std::string_view name);

RENDER_API bool shader_abi_binding_matches(
    const ShaderAbiResourceDecl& decl,
    const tc_shader_resource_binding& binding);

RENDER_API const tc_shader_resource_binding* find_shader_abi_resource_binding(
    const tc_shader* shader,
    ShaderAbiResourceId id);

} // namespace termin
