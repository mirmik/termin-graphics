// render_engine.hpp - Core render engine for executing pipeline passes
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "termin/render/render_camera.hpp"
#include "termin/render/render_execution_capabilities.hpp"
#include "termin/render/render_export.hpp"
#include "termin/render/render_item_snapshot.hpp"
#include "termin/render/material_texture_source.hpp"
#include "termin/render/render_pipeline.hpp"
#include <termin/geom/rect2.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/handles.hpp>

extern "C" {
#include "render/tc_frame_graph.h"
}

// tgfx2 forward declarations. Windowed hosts inject the canonical graphics
// domain; standalone/headless engines may create one explicitly.
namespace tgfx {
    class IRenderDevice;
    class RenderContext2;
    class GraphicsHost;
} // namespace tgfx

namespace termin {

    class ShaderArtifactResolver;
    struct FrameGraphCaptureRequest;

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
        RenderViewState view;
        // Render extent for this target. This is not the UI viewport's screen
        // rectangle; display placement is handled later during present/blit.
        Rect2i render_rect{0, 0, 0, 0};
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

        // Scene-local resolutions for symbolic material texture sources.
        ResolvedMaterialTextureSources material_texture_sources;
    };

    // One immutable input bundle for a logical render target. Every pointer is
    // borrowed and must remain valid until execute_pipeline() returns.
    struct RenderExecutionTarget {
        const RenderTargetContext* context = nullptr;
        const RenderItemSnapshot* render_items = nullptr;
        const RenderExecutionCapabilities* capabilities = nullptr;
    };

    // Scene-neutral pipeline execution request. Adapters own source traversal,
    // immutable snapshot publication and concrete capability lifetimes.
    struct RenderExecution {
        RenderPipeline* pipeline = nullptr;
        std::unordered_map<std::string, RenderExecutionTarget> targets;
        std::string default_render_target;
        std::vector<FrameGraphCaptureRequest*> debug_capture_requests;
    };

    class RENDER_CORE_API RenderEngine {
    private:
        std::unique_ptr<tgfx::GraphicsHost> owned_graphics_host_;
        tgfx::GraphicsHost* graphics_host_ = nullptr;
        std::unique_ptr<ShaderArtifactResolver> shader_artifact_resolver_;

    public:
        void set_graphics_host(tgfx::GraphicsHost& graphics_host);
        void ensure_tgfx2();

        // Access the engine's tgfx2 render context. May return nullptr if
        // ensure_tgfx2() has not yet been called or TERMIN_DISABLE_TGFX2 is
        // set. The returned pointer belongs to the injected/owned GraphicsHost.
        tgfx::RenderContext2* tgfx2_ctx();

        // Access the tgfx2 render device that owns all texture/buffer
        // handles used by the engine. Lifetime-tied to the RenderEngine.
        tgfx::IRenderDevice* tgfx2_device();

        void configure_shader_artifacts(const std::string& artifact_root,
                                        const std::string& cache_root,
                                        const std::string& compiler_path,
                                        bool dev_compile_enabled,
                                        ShaderArtifactResolver::ReadCallback read_callback = {});

        RenderPipelineCacheStats pipeline_cache_stats() const;

    public:
        RenderEngine();
        ~RenderEngine();

        void execute_pipeline(const RenderExecution& execution);
    };

} // namespace termin
