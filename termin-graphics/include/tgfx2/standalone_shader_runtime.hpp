#pragma once

#include "tgfx2/tgfx2_api.h"

namespace tgfx {

    class GraphicsHost;

    // Configure writable, lazily compiled shader artifacts for a standalone
    // application that owns a GraphicsHost directly. Engine and packaged-runtime
    // composition roots must continue to provide their explicit artifact resolver.
    //
    // Tool lookup order is:
    //   1. TERMIN_SHADERC / TERMIN_SLANGC
    //   2. TERMIN_SDK/bin
    //   3. the SDK/build tree containing termin_graphics2
    //   4. PATH
    //
    // Generated files live below TERMIN_SDK_SHADER_CACHE_ROOT when set, otherwise
    // below the platform user cache directory. The function logs an actionable
    // error and returns false when tools or a writable cache are unavailable.
    TGFX2_API bool configure_default_standalone_shader_runtime(GraphicsHost& host, const char* label);

} // namespace tgfx
