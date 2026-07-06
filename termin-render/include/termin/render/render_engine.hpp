// render_engine.hpp - Core render engine for executing pipeline passes
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <tgfx2/enums.hpp>
#include <tgfx2/handles.hpp>
#include "termin/render/frame_pass.hpp"
#include "termin/render/execute_context.hpp"
#include "termin/render/render_camera.hpp"
#include "termin/render/render_pipeline.hpp"
#include "termin/render/render_export.hpp"
#include <termin/lighting/light.hpp>
#include "termin/lighting/shadow.hpp"
#include <termin/tc_scene.hpp>

extern "C" {
#include "render/tc_frame_graph.h"
#include "core/tc_scene.h"
}

// tgfx2 forward declarations — RenderEngine lazily owns an OpenGL device,
// pipeline cache, and mid-level RenderContext2 that migrated passes use via
// ExecuteContext::ctx2 (see Phase 2 of tgfx2 migration).
namespace tgfx {
class IRenderDevice;
class RenderContext2;
class RenderRuntime;
}

namespace termin {

struct RenderPipelineCacheStats {
    uint64_t hit_count = 0;
    uint64_t miss_count = 0;
    uint64_t create_pipeline_count = 0;
    uint64_t unique_vertex_layout_signature_count = 0;
    size_t cached_pipeline_count = 0;
    std::vector<size_t> vertex_layout_signature_hashes;
};

struct RenderTargetContext {
public:
    std::string name;
    RenderCamera camera;
    // Render extent for this target. This is not the UI viewport's screen
    // rectangle; display placement is handled later during present/blit.
    Rect4i render_rect{0, 0, 0, 0};
    tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t render_category_mask = 0xFFFFFFFFFFFFFFFFULL;

    // Final render target — native tgfx2 color + depth textures owned
    // by the caller (typically a render target or legacy viewport state). Passes in the
    // pipeline receive these through ExecuteContext::tex2_writes under
    // the OUTPUT/DISPLAY alias.
    tgfx::TextureHandle output_color_tex;
    tgfx::TextureHandle output_depth_tex;
    tgfx::PixelFormat output_color_format = tgfx::PixelFormat::RGBA8_UNorm;
    tgfx::PixelFormat output_depth_format = tgfx::PixelFormat::D24_UNorm;
    bool clear_color_enabled = false;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool clear_depth_enabled = false;
    float clear_depth = 1.0f;

    // External graph inputs keyed by External RT slot name. These are
    // supplied by RenderTarget.pipeline_params.
    std::unordered_map<std::string, tgfx::TextureHandle> external_textures;
};

class RENDER_API RenderEngine {
private:
    // Lazily constructed tgfx2 runtime. It either owns a standalone
    // device or borrows the host-owned process-wide interop device.
    std::unique_ptr<tgfx::RenderRuntime> tgfx2_runtime_;

public:
    void ensure_tgfx2();

    // Access the engine's tgfx2 render context. May return nullptr if
    // ensure_tgfx2() has not yet been called or TERMIN_DISABLE_TGFX2 is
    // set. The returned pointer remains owned by the RenderEngine.
    tgfx::RenderContext2* tgfx2_ctx();

    // Access the tgfx2 render device that owns all texture/buffer
    // handles used by the engine. Lifetime-tied to the RenderEngine.
    tgfx::IRenderDevice* tgfx2_device();

    RenderPipelineCacheStats pipeline_cache_stats() const;

public:
    RenderEngine();
    ~RenderEngine();

    void render_scene_pipeline_offscreen(
        RenderPipeline& pipeline,
        tc_scene_handle scene,
        const std::unordered_map<std::string, RenderTargetContext>& render_target_contexts,
        const std::vector<Light>& lights,
        const std::string& default_render_target = ""
    );
};

} // namespace termin
