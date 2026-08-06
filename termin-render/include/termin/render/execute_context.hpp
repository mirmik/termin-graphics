#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/render/frame_pass.hpp>
#include <termin/render/render_camera.hpp>
#include <termin/render/render_export.hpp>
#include <termin/lighting/light.hpp>
#include <termin/tc_scene.hpp>
#include <core/tc_entity_pool.h>

#include <tgfx2/descriptors.hpp>

namespace tgfx {
class RenderContext2;
}

namespace termin {

class ShadowMapArrayResource;
class RenderSceneItemSnapshot;
struct FrameGraphCaptureRequest;

// Per-resource tgfx2 texture map. Passes that draw through ctx2
// consume entries from tex2_reads/tex2_writes (and the depth variants
// below) directly.
using Tex2Map = std::unordered_map<std::string, tgfx::TextureHandle>;

// Non-FBO framegraph resources indexed by canonical name. Currently
// only populated for shadow_map_array resources — ShadowPass writes
// into one, ColorPass reads from one.
using ShadowArrayMap = std::unordered_map<std::string, ShadowMapArrayResource*>;

// Pass-local declaration of one named framegraph color output. Array order is
// shader-visible: entry N maps to fragment output location N.
struct FrameGraphColorAttachment {
    const char* resource_name = nullptr;
    tgfx::LoadOp load = tgfx::LoadOp::Clear;
    tgfx::StoreOp store = tgfx::StoreOp::Store;
    float clear_color[4] = {0, 0, 0, 0};
};

struct FrameGraphDepthAttachment {
    const char* resource_name = nullptr;
    tgfx::LoadOp load = tgfx::LoadOp::Clear;
    tgfx::StoreOp store = tgfx::StoreOp::Store;
    float clear_depth = 1.0f;
    uint32_t clear_stencil = 0;
};

struct ExecuteContext {
public:
    tgfx::RenderContext2* ctx2 = nullptr;
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
    // Render extent for the pass. This is not a display viewport rectangle.
    Rect2i render_rect;
    TcSceneRef scene;
    RenderCamera* camera = nullptr;
    const StereoRenderViews* stereo_views = nullptr;
    std::string render_target_name;
    tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
    std::vector<Light> lights;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t render_category_mask = 0xFFFFFFFFFFFFFFFFULL;
    // Borrowed from RenderEngine for this scene/view execution. Immutable
    // after its first successful collection and shared by all geometry passes.
    RenderSceneItemSnapshot* render_item_snapshot = nullptr;
    // Frame-local debugger requests for this pass. These pointers are valid
    // only until the enclosing RenderEngine execution returns.
    std::vector<FrameGraphCaptureRequest*> debug_internal_capture_requests;

    RENDER_API const std::string* requested_internal_symbol() const;
    RENDER_API bool should_capture_internal(const char* symbol) const;
    RENDER_API bool capture_internal(
        const char* symbol,
        tgfx::TextureHandle texture,
        int width = 0,
        int height = 0,
        tgfx::PixelFormat format = tgfx::PixelFormat::RGBA8_UNorm
    );

    // Resolves independent named framegraph resources into the backend-neutral
    // ordered attachment contract consumed by RenderContext2. This does not
    // create or cache an FBO-like aggregate; the composition exists only for
    // the pass invocation.
    RENDER_API bool build_render_pass(
        std::span<const FrameGraphColorAttachment> colors,
        const FrameGraphDepthAttachment* depth,
        tgfx::RenderPassDesc& out_pass
    ) const;
};

} // namespace termin
