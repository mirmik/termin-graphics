// shader_resource_apply.hpp - layout-first binding helpers for engine resources.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tgfx2/handles.hpp"

extern "C" {
#include "tgfx/resources/tc_shader.h"
}

namespace tgfx {
class RenderContext2;
}

namespace termin {

bool shader_layout_present(const tc_shader* shader);

bool bind_lighting_ubo_for_shader(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    tgfx::BufferHandle lighting_ubo);

bool bind_shadow_block_for_shader(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    const void* data,
    uint32_t size);

bool bind_shadow_maps_for_shader(
    tgfx::RenderContext2& ctx,
    const tc_shader* shader,
    std::span<const tgfx::TextureHandle> shadow_maps,
    tgfx::SamplerHandle sampler,
    size_t max_count);

} // namespace termin
