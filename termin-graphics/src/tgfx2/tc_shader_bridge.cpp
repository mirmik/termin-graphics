// tc_shader_bridge.cpp - TcShader ↔ tgfx2 interop.
#include "tgfx2/tc_shader_bridge.hpp"

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include "tgfx/tc_gpu_context.h"
#include "tgfx/tc_gpu_share_group.h"
#include "tgfx/tgfx2_interop.h"
#include <tcbase/tc_log.h>
}

namespace termin {

static std::string g_shader_artifact_root;

void tgfx2_set_shader_artifact_root(const char* root) {
    g_shader_artifact_root = root ? root : "";
}

const char* tgfx2_get_shader_artifact_root(void) {
    if (!g_shader_artifact_root.empty()) {
        return g_shader_artifact_root.c_str();
    }
    const char* env = std::getenv("TERMIN_SHADER_ARTIFACT_ROOT");
    return env ? env : "";
}

static const char* stage_extension(tgfx::ShaderStage stage) {
    switch (stage) {
        case tgfx::ShaderStage::Vertex: return "vert";
        case tgfx::ShaderStage::Fragment: return "frag";
        case tgfx::ShaderStage::Geometry: return "geom";
        case tgfx::ShaderStage::Compute: return "comp";
    }
    return "spv";
}

bool tgfx2_load_shader_artifact(
    const char* shader_uuid,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    const char* root = tgfx2_get_shader_artifact_root();
    if (!shader_uuid || shader_uuid[0] == '\0') {
        tc_log(TC_LOG_ERROR,
               "tc_shader_ensure_tgfx2: cannot load SPIR-V artifact, shader_uuid='%s'",
               shader_uuid ? shader_uuid : "<null>");
        return false;
    }
    if (!root || root[0] == '\0') {
        return false;
    }

    std::string path = std::string(root) + "/shaders/vulkan/"
        + shader_uuid + "." + stage_extension(stage) + ".spv";

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: missing shader artifact '%s'", path.c_str());
        return false;
    }

    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.empty()) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: empty shader artifact '%s'", path.c_str());
        return false;
    }
    return true;
}

static bool load_shader_artifact(
    const tc_shader* shader,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    return tgfx2_load_shader_artifact(shader ? shader->uuid : nullptr, stage, out);
}

// Reinterpret the slot's opaque `tgfx2_shader_device` as an IRenderDevice
// pointer. The slot stores whatever device instance first populated it.
static tgfx::IRenderDevice* slot_device(const tc_gpu_slot* slot) {
    if (!slot || !slot->tgfx2_shader_device) return nullptr;
    return static_cast<tgfx::IRenderDevice*>(slot->tgfx2_shader_device);
}

static void destroy_cached_tgfx2_shaders(tc_gpu_slot* slot) {
    if (!slot) return;
    tgfx::IRenderDevice* dev = slot_device(slot);
    // Skip the destroy calls when the cached device is no longer the
    // live interop target — Python interpreter shutdown tears down
    // BackendWindow (and its device) before the shader registry gets
    // cleared, leaving slot->tgfx2_shader_device dangling. Calling
    // destroy on a dead device segfaults. Leaking at shutdown is OK:
    // the GPU driver reclaims resources on process exit anyway.
    void* live = tgfx2_interop_get_device();
    if (dev && dev == live) {
        if (slot->tgfx2_shader_vs_id != 0) {
            tgfx::ShaderHandle h;
            h.id = slot->tgfx2_shader_vs_id;
            dev->destroy(h);
        }
        if (slot->tgfx2_shader_fs_id != 0) {
            tgfx::ShaderHandle h;
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
    tgfx::IRenderDevice* device,
    tgfx::ShaderHandle* out_vs,
    tgfx::ShaderHandle* out_fs)
{
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: shader is NULL");
        return false;
    }
    if (!device) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: device is NULL");
        return false;
    }
    if (!out_fs) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: out_fs is NULL");
        return false;
    }

    // Ensure a tc_gpu_context is active for slot lookup. Callers that
    // own real GPU contexts (editor WindowManager, engine rendering
    // manager) set their own via tc_gpu_set_context before draw; for
    // standalone paths (launcher UI, examples) there's no such setup,
    // so we fall back to a process-wide default. This lets hash-based
    // shader caching work everywhere without forcing every caller to
    // thread GPU-context plumbing through.
    tc_ensure_default_gpu_context();
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

    // FS-only shaders (vertex_source == NULL) are used by fullscreen passes
    // passes that share the built-in FSQ vertex shader from RenderContext2.
    // The slot caches just the FS module; VS id stays 0 and callers pass
    // nullptr for out_vs.
    const bool has_vs = (shader->vertex_source != nullptr
                         && shader->vertex_source[0] != '\0');

    // Cache hit: FS populated, version and device match. For non-FS-only
    // shaders, VS must also be populated.
    if (slot->tgfx2_shader_fs_id != 0 &&
        slot->tgfx2_shader_version == static_cast<int32_t>(shader->version) &&
        slot->tgfx2_shader_device == static_cast<void*>(device) &&
        (!has_vs || slot->tgfx2_shader_vs_id != 0)) {
        if (out_vs) out_vs->id = slot->tgfx2_shader_vs_id;
        out_fs->id = slot->tgfx2_shader_fs_id;
        return true;
    }

    // Miss. Destroy previous cached handles through whichever device they
    // were created on, then recompile for the incoming device.
    destroy_cached_tgfx2_shaders(slot);

    if (!shader->fragment_source) {
        tc_log(TC_LOG_ERROR,
               "tc_shader_ensure_tgfx2: missing fragment_source for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    tgfx::ShaderHandle vs;
    if (has_vs) {
        tgfx::ShaderDesc vs_desc;
        vs_desc.stage = tgfx::ShaderStage::Vertex;
        vs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":vertex";
        if (device->backend_type() == tgfx::BackendType::Vulkan
            && load_shader_artifact(shader, vs_desc.stage, vs_desc.bytecode)) {
        } else {
            vs_desc.source = shader->vertex_source;
        }
        vs = device->create_shader(vs_desc);
        if (!vs) {
            tc_log(TC_LOG_ERROR,
                   "tc_shader_ensure_tgfx2: VS compile failed for '%s'",
                   shader->name ? shader->name : shader->uuid);
            return false;
        }
    }

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":fragment";
    if (device->backend_type() == tgfx::BackendType::Vulkan
        && load_shader_artifact(shader, fs_desc.stage, fs_desc.bytecode)) {
    } else {
        fs_desc.source = shader->fragment_source;
    }
    tgfx::ShaderHandle fs = device->create_shader(fs_desc);
    if (!fs) {
        // Roll back VS to avoid leaking on partial failure.
        if (has_vs) device->destroy(vs);
        tc_log(TC_LOG_ERROR,
               "tc_shader_ensure_tgfx2: FS compile failed for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    slot->tgfx2_shader_vs_id = has_vs ? vs.id : 0;
    slot->tgfx2_shader_fs_id = fs.id;
    slot->tgfx2_shader_version = static_cast<int32_t>(shader->version);
    slot->tgfx2_shader_device = static_cast<void*>(device);

    if (out_vs) out_vs->id = has_vs ? vs.id : 0;
    out_fs->id = fs.id;
    return true;
}

} // namespace termin
