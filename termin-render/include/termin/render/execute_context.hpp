#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/render/frame_pass.hpp>
#include <termin/render/render_camera.hpp>
#include <termin/render/light.hpp>
#include <termin/tc_scene.hpp>
#include <core/tc_entity_pool.h>

// Forward declaration — tgfx2 is the new backend-neutral graphics API.
// Passes migrated to Phase 2 use ctx2 instead of graphics. Legacy passes
// continue to use graphics; both pointers are valid simultaneously during
// the dual-path migration window.
namespace tgfx2 {
class RenderContext2;
}

namespace termin {

struct ExecuteContext {
public:
    GraphicsBackend* graphics = nullptr;
    tgfx2::RenderContext2* ctx2 = nullptr;
    FBOMap reads_fbos;
    FBOMap writes_fbos;
    Rect4i rect;
    TcSceneRef scene;
    RenderCamera* camera = nullptr;
    std::string viewport_name;
    tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
    std::vector<Light> lights;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL;
};

} // namespace termin
