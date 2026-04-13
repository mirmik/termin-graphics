// tc_shader_bridge.cpp - TcShader ↔ tgfx2 interop.
#include "tgfx2/tc_shader_bridge.hpp"

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include "tgfx/tc_gpu_context.h"
#include "tgfx/tc_gpu_share_group.h"
#include <tcbase/tc_log.h>
}

namespace termin {

// Reinterpret the slot's opaque `tgfx2_shader_device` as an IRenderDevice
// pointer. The slot stores whatever device instance first populated it.
static tgfx2::IRenderDevice* slot_device(const tc_gpu_slot* slot) {
    if (!slot || !slot->tgfx2_shader_device) return nullptr;
    return static_cast<tgfx2::IRenderDevice*>(slot->tgfx2_shader_device);
}

static void destroy_cached_tgfx2_shaders(tc_gpu_slot* slot) {
    if (!slot) return;
    tgfx2::IRenderDevice* dev = slot_device(slot);
    if (dev) {
        if (slot->tgfx2_shader_vs_id != 0) {
            tgfx2::ShaderHandle h;
            h.id = slot->tgfx2_shader_vs_id;
            dev->destroy(h);
        }
        if (slot->tgfx2_shader_fs_id != 0) {
            tgfx2::ShaderHandle h;
            h.id = slot->tgfx2_shader_fs_id;
            dev->destroy(h);
        }
    }
    slot->tgfx2_shader_vs_id = 0;
    slot->tgfx2_shader_fs_id = 0;
    slot->tgfx2_shader_version = -1;
    slot->tgfx2_shader_device = nullptr;
}

bool tc_shader_ensure_tgfx2(
    ::tc_shader* shader,
    tgfx2::IRenderDevice* device,
    tgfx2::ShaderHandle* out_vs,
    tgfx2::ShaderHandle* out_fs)
{
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: shader is NULL");
        return false;
    }
    if (!device) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: device is NULL");
        return false;
    }
    if (!out_vs || !out_fs) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: out handles are NULL");
        return false;
    }

    tc_gpu_context* ctx = tc_gpu_get_context();
    if (!ctx) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: no GPU context set");
        return false;
    }

    tc_gpu_slot* slot = tc_gpu_context_shader_slot(ctx, shader->pool_index);
    if (!slot) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: failed to get slot");
        return false;
    }

    // Cache hit: handles populated, version matches current, same device.
    if (slot->tgfx2_shader_vs_id != 0 &&
        slot->tgfx2_shader_fs_id != 0 &&
        slot->tgfx2_shader_version == static_cast<int32_t>(shader->version) &&
        slot->tgfx2_shader_device == static_cast<void*>(device)) {
        out_vs->id = slot->tgfx2_shader_vs_id;
        out_fs->id = slot->tgfx2_shader_fs_id;
        return true;
    }

    // Miss. Destroy previous cached handles through whichever device they
    // were created on, then recompile for the incoming device.
    destroy_cached_tgfx2_shaders(slot);

    if (!shader->vertex_source || !shader->fragment_source) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_ensure_tgfx2: missing sources for '%s' (vs=%p, fs=%p)",
               shader->name ? shader->name : shader->uuid,
               static_cast<const void*>(shader->vertex_source),
               static_cast<const void*>(shader->fragment_source));
        return false;
    }

    tgfx2::ShaderDesc vs_desc;
    vs_desc.stage = tgfx2::ShaderStage::Vertex;
    vs_desc.source = shader->vertex_source;
    tgfx2::ShaderHandle vs = device->create_shader(vs_desc);
    if (!vs) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_ensure_tgfx2: VS compile failed for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    tgfx2::ShaderDesc fs_desc;
    fs_desc.stage = tgfx2::ShaderStage::Fragment;
    fs_desc.source = shader->fragment_source;
    tgfx2::ShaderHandle fs = device->create_shader(fs_desc);
    if (!fs) {
        // Roll back VS to avoid leaking on partial failure.
        device->destroy(vs);
        tc_log(TC_LOG_ERROR,
               "tc_shader_ensure_tgfx2: FS compile failed for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    slot->tgfx2_shader_vs_id = vs.id;
    slot->tgfx2_shader_fs_id = fs.id;
    slot->tgfx2_shader_version = static_cast<int32_t>(shader->version);
    slot->tgfx2_shader_device = static_cast<void*>(device);

    *out_vs = vs;
    *out_fs = fs;
    return true;
}

} // namespace termin
