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

namespace tgfx2 {
class RenderContext2;
}

namespace termin {

class ShadowMapArrayResource;

// Per-resource tgfx2 texture map. Passes that draw through ctx2
// consume entries from tex2_reads/tex2_writes (and the depth variants
// below) directly.
using Tex2Map = std::unordered_map<std::string, tgfx2::TextureHandle>;

// Non-FBO framegraph resources indexed by canonical name. Currently
// only populated for shadow_map_array resources — ShadowPass writes
// into one, ColorPass reads from one.
using ShadowArrayMap = std::unordered_map<std::string, ShadowMapArrayResource*>;

struct ExecuteContext {
public:
    tgfx2::RenderContext2* ctx2 = nullptr;
    // Color attachments of pipeline resources as tgfx2 textures.
    Tex2Map tex2_reads;
    Tex2Map tex2_writes;
    // Depth attachments. Only populated for resources with a depth
    // texture; empty entry = no depth texture available.
    Tex2Map tex2_depth_reads;
    Tex2Map tex2_depth_writes;
    // Non-FBO framegraph resources (shadow map arrays). Keyed by
    // canonical name; same key serves reads and writes since shadow
    // arrays are written by one pass and read by another.
    ShadowArrayMap shadow_arrays;
    Rect4i rect;
    TcSceneRef scene;
    RenderCamera* camera = nullptr;
    std::string viewport_name;
    tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
    std::vector<Light> lights;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL;
};

} // namespace termin
