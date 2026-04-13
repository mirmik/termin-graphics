// tc_shader_bridge.hpp - TcShader ↔ tgfx2 interop.
//
// Lazily compiles a tc_shader into a pair of tgfx2 ShaderHandles
// (vertex + fragment) and caches them on the shader's per-context slot.
// Coexists with the legacy glUniform-based path (tc_shader_compile_gpu):
// both produce independent artifacts from the same source strings, both
// respect the shader->version counter for hot reload.
//
// Usage from a migrated pass:
//
//     tgfx2::ShaderHandle vs, fs;
//     if (termin::tc_shader_ensure_tgfx2(tc_shader_ptr, &ctx.ctx2->device(),
//                                        &vs, &fs)) {
//         ctx.ctx2->bind_shader(vs, fs);
//     }
#pragma once

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/handles.hpp"

extern "C" {
struct tc_shader;
}

namespace tgfx2 {
class IRenderDevice;
}

namespace termin {

// Ensure the tgfx2 vertex and fragment ShaderHandles for `shader` are live
// and up to date for the given device. Compiles on first call or when the
// shader version has advanced past the cached one; destroys stale handles
// before recompiling.
//
// Returns true on success (out handles valid), false on any error
// (missing sources, compile failure, NULL arguments). On failure, out
// arguments are left in whatever state they had on entry.
TGFX2_API bool tc_shader_ensure_tgfx2(
    ::tc_shader* shader,
    tgfx2::IRenderDevice* device,
    tgfx2::ShaderHandle* out_vs,
    tgfx2::ShaderHandle* out_fs);

} // namespace termin
