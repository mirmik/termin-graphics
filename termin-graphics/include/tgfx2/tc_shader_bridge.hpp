// tc_shader_bridge.hpp - TcShader ↔ tgfx2 interop.
//
// Lazily compiles a tc_shader into a pair of tgfx2 ShaderHandles
// (vertex + fragment) through IRenderDevice::ensure_tc_shader().
// Produces independent backend artifacts from the same source strings and
// respects the shader->version counter for hot reload. The tgfx2 handles are
// owned by the render device cache.
//
// Usage from a migrated pass:
//
//     tgfx::ShaderHandle vs, fs;
//     if (termin::tc_shader_ensure_tgfx2(tc_shader_ptr, &ctx.ctx2->device(),
//                                        &vs, &fs)) {
//         ctx.ctx2->bind_shader(vs, fs);
//     }
#pragma once

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
struct tc_shader;
}

namespace tgfx {
class IRenderDevice;
}

namespace termin {

// Ensure the tgfx2 vertex and fragment ShaderHandles for `shader` are live
// and up to date for the given device. The device compiles on first call or
// when the shader version has advanced past the cached one; stale handles
// are destroyed by the device cache before recompiling.
//
// Returns true on success (out handles valid), false on any error
// (missing sources, compile failure, NULL arguments). On failure, out
// arguments are left in whatever state they had on entry.
TGFX2_API bool tc_shader_ensure_tgfx2(
    ::tc_shader* shader,
    tgfx::IRenderDevice* device,
    tgfx::ShaderHandle* out_vs,
    tgfx::ShaderHandle* out_fs);

TGFX2_API void tgfx2_set_shader_artifact_root(const char* root);
TGFX2_API const char* tgfx2_get_shader_artifact_root(void);
TGFX2_API bool tgfx2_shader_artifact_path(
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::string& out);
TGFX2_API bool tgfx2_load_shader_artifact_for_backend(
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out);
TGFX2_API bool tgfx2_load_shader_artifact(
    const char* shader_uuid,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out);

} // namespace termin
