#include "termin/render/material_ubo_runtime.hpp"

#include "termin/render/tgfx2_bridge.hpp"

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

#include <tcbase/tc_log.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace termin {
namespace {

constexpr uint32_t MATERIAL_TEXTURE_BINDING_BASE = 4;
constexpr uint32_t MATERIAL_TEXTURE_BINDING_SHADOW_SLOT = 8;

uint32_t material_texture_binding_for_index(uint32_t index) {
    uint32_t binding = MATERIAL_TEXTURE_BINDING_BASE + index;
    if (binding >= MATERIAL_TEXTURE_BINDING_SHADOW_SLOT) {
        binding += 1;
    }
    return binding;
}

void write_bytes(uint8_t* dst, const void* src, size_t bytes) {
    std::memcpy(dst, src, bytes);
}

const tc_uniform_value* find_uniform(const tc_material_phase* phase, const char* name) {
    if (!phase || !name) return nullptr;
    for (size_t i = 0; i < phase->uniform_count; ++i) {
        const tc_uniform_value* uniform = &phase->uniforms[i];
        if (std::strncmp(uniform->name, name, TC_UNIFORM_NAME_MAX) == 0) {
            return uniform;
        }
    }
    return nullptr;
}

bool pack_uniform_value(
    const tc_material_ubo_entry& entry,
    const tc_uniform_value& uniform,
    uint8_t* dst)
{
    const char* type = entry.property_type;

    if (std::strcmp(type, "Float") == 0) {
        if (uniform.type != TC_UNIFORM_FLOAT) return false;
        write_bytes(dst, &uniform.data.f, sizeof(float));
        return true;
    }
    if (std::strcmp(type, "Int") == 0) {
        if (uniform.type != TC_UNIFORM_INT) return false;
        write_bytes(dst, &uniform.data.i, sizeof(int32_t));
        return true;
    }
    if (std::strcmp(type, "Bool") == 0) {
        if (uniform.type != TC_UNIFORM_BOOL) return false;
        int32_t value = uniform.data.i != 0 ? 1 : 0;
        write_bytes(dst, &value, sizeof(int32_t));
        return true;
    }
    if (std::strcmp(type, "Vec2") == 0) {
        if (uniform.type != TC_UNIFORM_VEC2) return false;
        write_bytes(dst, uniform.data.v2, sizeof(float) * 2);
        return true;
    }
    if (std::strcmp(type, "Vec3") == 0) {
        if (uniform.type != TC_UNIFORM_VEC3) return false;
        write_bytes(dst, uniform.data.v3, sizeof(float) * 3);
        return true;
    }
    if (std::strcmp(type, "Vec4") == 0 || std::strcmp(type, "Color") == 0) {
        if (uniform.type != TC_UNIFORM_VEC4) return false;
        write_bytes(dst, uniform.data.v4, sizeof(float) * 4);
        return true;
    }
    if (std::strcmp(type, "Mat4") == 0) {
        if (uniform.type != TC_UNIFORM_MAT4) return false;
        write_bytes(dst, uniform.data.m4, sizeof(float) * 16);
        return true;
    }

    return false;
}

void pack_material_ubo(
    const tc_material_phase* phase,
    const tc_shader* shader,
    std::vector<uint8_t>& staging)
{
    if (!phase || !shader || !shader->material_ubo_entries) return;
    for (uint32_t i = 0; i < shader->material_ubo_entry_count; ++i) {
        const tc_material_ubo_entry& entry = shader->material_ubo_entries[i];
        const tc_uniform_value* uniform = find_uniform(phase, entry.name);
        if (!uniform) continue;

        uint64_t end = static_cast<uint64_t>(entry.offset) + entry.size;
        if (end > staging.size()) {
            tc::Log::error(
                "material UBO entry '%s' is outside block: offset=%u size=%u block=%zu",
                entry.name,
                entry.offset,
                entry.size,
                staging.size());
            continue;
        }

        pack_uniform_value(entry, *uniform, staging.data() + entry.offset);
    }
}

void bind_material_phase_textures_runtime(
    tc_material_phase* phase,
    uint32_t tex_slot_start,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx)
{
    if (!phase) return;
    (void)tex_slot_start;

    for (size_t i = 0; i < phase->texture_count; ++i) {
        const tc_material_texture& mat_tex = phase->textures[i];
        if (tc_texture_handle_is_invalid(mat_tex.texture)) {
            continue;
        }
        tgfx::TextureHandle tex2 = wrap_tc_texture_as_tgfx2(device, mat_tex.texture);
        if (tex2) {
            ctx.bind_sampled_texture(material_texture_binding_for_index(static_cast<uint32_t>(i)), tex2);
        }
    }
}

} // namespace

bool apply_material_phase_ubo_runtime(
    tc_material_phase* phase,
    const tc_shader* shader,
    uint32_t ubo_slot,
    uint32_t tex_slot_start,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx)
{
    if (!phase || !shader) return false;
    bind_material_phase_textures_runtime(phase, tex_slot_start, device, ctx);

    if (shader->material_ubo_block_size == 0) return false;

    std::vector<uint8_t> staging(shader->material_ubo_block_size, 0);
    pack_material_ubo(phase, shader, staging);

    ctx.bind_uniform_buffer_ring(
        ubo_slot,
        staging.data(),
        static_cast<uint32_t>(staging.size()));

    return true;
}

} // namespace termin
