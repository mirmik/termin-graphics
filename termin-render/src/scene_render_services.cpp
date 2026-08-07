#include <termin/render/scene_render_services.hpp>

#include <tcbase/tc_log.hpp>
#include <termin/render/execute_context.hpp>

namespace termin {

const SceneRenderServices* require_scene_render_services(
    const ExecuteContext& context,
    const char* consumer)
{
    const char* name = consumer ? consumer : "SceneRenderServices";
    if (!context.scene_services) {
        tc::Log::error("[%s] render execution has no SceneRenderServices capability", name);
        return nullptr;
    }
    if (!context.scene_services->scene.valid()) {
        tc::Log::error("[%s] SceneRenderServices contains an invalid scene", name);
        return nullptr;
    }
    return context.scene_services;
}

} // namespace termin
