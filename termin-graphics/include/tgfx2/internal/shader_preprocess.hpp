// shader_preprocess.hpp - Shared GLSL preprocessor hook for all backends.
//
// The legacy tgfx path exposes a global `tgfx_gpu_set_shader_preprocess(fn)`
// callback (registered by termin-app's GlslPreprocessor on startup) that
// resolves `#include "foo.glsl"` / removes `@features` directives / etc.
// OpenGL and Vulkan backends both call through the same hook so a shader
// compiled on one backend matches its counterpart on the other.
#pragma once

#include <cstdlib>
#include <string>

extern "C" {
#include "tgfx/tgfx_resource_gpu.h"
}

namespace tgfx::internal {

// Resolve #include / other directives via the globally-registered
// preprocessor. Returns `source` unchanged when no preprocessor is
// installed or the callback returned NULL.
//
// `source_name` is only used for diagnostics (line tags in includes).
inline std::string preprocess_shader_source(const std::string& source,
                                            const char* source_name = "<tgfx2 shader>") {
    tgfx_shader_preprocess_fn preprocess = tgfx_gpu_get_shader_preprocess();
    if (!preprocess) {
        return source;
    }
    char* resolved = preprocess(source.c_str(), source_name);
    if (!resolved) {
        return source;
    }
    std::string out(resolved);
    std::free(resolved);
    return out;
}

} // namespace tgfx::internal
