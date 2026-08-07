#pragma once

#include <functional>

#include <termin/render/render_export.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

extern "C" {
#include <core/tc_scene_pool.h>
}

namespace termin {

// Scene adapter capability for offline shader export. It intentionally lives
// outside CxxFramePass so generic pass interfaces do not depend on tc_scene.
class RENDER_API SceneShaderUsageProvider {
public:
    virtual ~SceneShaderUsageProvider() = default;

    virtual void collect_scene_shader_usages(
        tc_scene_handle scene,
        const std::function<void(TcShader)>& emit) const = 0;
};

} // namespace termin
