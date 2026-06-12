#include "termin/render/shader_resource_apply.hpp"
#include "termin/render/shader_binding_policy.hpp"

#include <algorithm>
#include <iterator>

#include "tgfx2/render_context.hpp"

extern "C" {
#include "tgfx/resources/tc_shader_registry.h"
}

namespace termin {
namespace {

const tc_shader_resource_binding* find_resource(
    const tc_shader* shader,
    std::span<const char* const> names,
    uint32_t expected_kind)
{
    if (!shader) return nullptr;
    for (const char* name : names) {
        const tc_shader_resource_binding* rb =
            tc_shader_find_resource_binding(shader, name);
        if (rb && rb->kind == expected_kind) {
            return rb;
        }
    }
    return nullptr;
}

bool allow_legacy_fallback(const tc_shader* shader) {
    return shader_allows_legacy_resource_fallback(shader);
}

} // namespace

bool shader_layout_present(const tc_shader* shader) {
    return shader_has_layout_metadata(shader);
}

bool bind_lighting_ubo_for_shader(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    tgfx::BufferHandle lighting_ubo,
    uint32_t fallback_binding)
{
    if (!lighting_ubo) return false;

    static constexpr const char* kNames[] = {
        "lighting",
        "lighting_ubo",
        "LightingBlock",
    };
    const tc_shader_resource_binding* rb = find_resource(
        shader,
        std::span<const char* const>(kNames, std::size(kNames)),
        TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    if (rb) {
        ctx.bind_uniform(rb->name, lighting_ubo);
        return true;
    }

    if (allow_legacy_fallback(shader)) {
        ctx.bind_uniform_buffer(fallback_binding, lighting_ubo);
        return true;
    }
    return false;
}

bool bind_shadow_block_for_shader(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    const void* data,
    uint32_t size,
    uint32_t fallback_binding)
{
    if (!data || size == 0) return false;

    static constexpr const char* kNames[] = {
        "shadow_block",
        "ShadowBlock",
    };
    const tc_shader_resource_binding* rb = find_resource(
        shader,
        std::span<const char* const>(kNames, std::size(kNames)),
        TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    if (rb) {
        ctx.bind_uniform_data(rb->name, data, size);
        return true;
    }

    if (allow_legacy_fallback(shader)) {
        ctx.bind_uniform_buffer_ring(fallback_binding, data, size);
        return true;
    }
    return false;
}

bool bind_shadow_maps_for_shader(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    std::span<const tgfx::TextureHandle> shadow_maps,
    tgfx::SamplerHandle sampler,
    uint32_t fallback_binding,
    size_t max_count)
{
    if (shadow_maps.empty()) return false;

    uint32_t binding = fallback_binding;
    static constexpr const char* kNames[] = {
        "shadow_maps",
        "u_shadow_map",
    };
    const tc_shader_resource_binding* rb = find_resource(
        shader,
        std::span<const char* const>(kNames, std::size(kNames)),
        TC_SHADER_RESOURCE_TEXTURE);
    if (rb) {
        (void)binding;
    } else if (!allow_legacy_fallback(shader)) {
        return false;
    }

    bool any_bound = false;
    const size_t count = std::min(shadow_maps.size(), max_count);
    for (size_t i = 0; i < count; ++i) {
        if (!shadow_maps[i]) continue;
        if (rb) {
            ctx.bind_texture_array_element(
                rb->name,
                static_cast<uint32_t>(i),
                shadow_maps[i],
                sampler);
        } else {
            ctx.bind_sampled_texture_array_element(
                binding,
                static_cast<uint32_t>(i),
                shadow_maps[i],
                sampler);
        }
        any_bound = true;
    }
    return any_bound;
}

} // namespace termin
