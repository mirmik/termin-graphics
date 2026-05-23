#include "termin/render/material_ubo_runtime.hpp"

#include "termin/render/tgfx2_bridge.hpp"

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

#include <tcbase/tc_log.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace termin {
namespace {

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

extern "C" void material_phase_runtime_release_ubo_cb(tc_material_phase* phase) {
    if (!phase || phase->ubo_id == 0 || !phase->ubo_device) return;
    auto* device = static_cast<tgfx::IRenderDevice*>(phase->ubo_device);
    tgfx::BufferHandle handle{};
    handle.id = phase->ubo_id;
    device->destroy(handle);
}

std::atomic<bool> g_release_cb_installed{false};

void ensure_release_cb_installed() {
    bool expected = false;
    if (g_release_cb_installed.compare_exchange_strong(expected, true)) {
        tc_material_phase_set_release_ubo_callback(material_phase_runtime_release_ubo_cb);
    }
}

tgfx::BufferHandle ensure_material_phase_ubo_runtime(
    tc_material_phase* phase,
    const tc_shader* shader,
    tgfx::IRenderDevice& device)
{
    if (!phase || !shader || shader->material_ubo_block_size == 0) return {};

    ensure_release_cb_installed();

    int32_t shader_version = static_cast<int32_t>(shader->version);
    bool stale =
        phase->ubo_id == 0 ||
        phase->ubo_size != shader->material_ubo_block_size ||
        phase->ubo_device != &device ||
        phase->ubo_version != shader_version;

    if (stale) {
        if (phase->ubo_id != 0 && phase->ubo_device == &device) {
            tgfx::BufferHandle old{};
            old.id = phase->ubo_id;
            device.destroy(old);
        } else if (phase->ubo_id != 0) {
            tc::Log::error("ensure_material_phase_ubo_runtime: phase UBO leak; device changed");
        }

        tgfx::BufferDesc desc;
        desc.size = shader->material_ubo_block_size;
        desc.usage = tgfx::BufferUsage::Uniform | tgfx::BufferUsage::CopyDst;
        tgfx::BufferHandle fresh = device.create_buffer(desc);

        phase->ubo_id = fresh.id;
        phase->ubo_size = shader->material_ubo_block_size;
        phase->ubo_version = shader_version;
        phase->ubo_device = &device;
        return fresh;
    }

    tgfx::BufferHandle existing{};
    existing.id = phase->ubo_id;
    return existing;
}

void bind_material_phase_textures_runtime(
    tc_material_phase* phase,
    uint32_t tex_slot_start,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx)
{
    if (!phase) return;

    uint32_t slot = tex_slot_start;
    for (size_t i = 0; i < phase->texture_count; ++i) {
        const tc_material_texture& mat_tex = phase->textures[i];
        if (tc_texture_handle_is_invalid(mat_tex.texture)) {
            ++slot;
            continue;
        }
        tgfx::TextureHandle tex2 = wrap_tc_texture_as_tgfx2(device, mat_tex.texture);
        if (tex2) {
            ctx.bind_sampled_texture(slot, tex2);
        }
        ++slot;
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

    tgfx::BufferHandle ubo = ensure_material_phase_ubo_runtime(phase, shader, device);
    if (!ubo) return false;

    std::vector<uint8_t> staging(shader->material_ubo_block_size, 0);
    pack_material_ubo(phase, shader, staging);

    if (device.ring_ubo_handle().id != 0) {
        ctx.bind_uniform_buffer_ring(
            ubo_slot,
            staging.data(),
            static_cast<uint32_t>(staging.size()));
    } else {
        device.upload_buffer(
            ubo,
            std::span<const uint8_t>(staging.data(), staging.size()));
        ctx.bind_uniform_buffer(ubo_slot, ubo);
    }

    return true;
}

} // namespace termin
