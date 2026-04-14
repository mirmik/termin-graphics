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

#include <tgfx2/handles.hpp>

// Forward declaration — tgfx2 is the new backend-neutral graphics API.
// Passes migrated to Phase 2 use ctx2 instead of graphics. Legacy passes
// continue to use graphics; both pointers are valid simultaneously during
// the dual-path migration window.
namespace tgfx2 {
class RenderContext2;
}

namespace termin {

// Per-resource tgfx2 texture map, parallel to FBOMap. Phase 2 passes that
// draw through ctx2 consume entries from tex2_reads/tex2_writes instead of
// wrapping FBOs themselves. Wrapping is owned by RenderEngine.
using Tex2Map = std::unordered_map<std::string, tgfx2::TextureHandle>;

struct ExecuteContext {
public:
    GraphicsBackend* graphics = nullptr;
    tgfx2::RenderContext2* ctx2 = nullptr;
    FBOMap reads_fbos;
    FBOMap writes_fbos;
    // Color attachments of pipeline resources as tgfx2 textures — the
    // canonical path for passes that draw through ctx2. Matches the
    // same resource names as reads_fbos / writes_fbos.
    Tex2Map tex2_reads;
    Tex2Map tex2_writes;
    // Depth attachments. Only populated for FBO resources that have a
    // depth texture (not for renderbuffer-backed depth or color-only
    // FBOs). Empty entry = no depth texture available for that name.
    Tex2Map tex2_depth_reads;
    Tex2Map tex2_depth_writes;
    Rect4i rect;
    TcSceneRef scene;
    RenderCamera* camera = nullptr;
    std::string viewport_name;
    tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
    std::vector<Light> lights;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL;
};

} // namespace termin
