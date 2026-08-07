#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/render/frame_pass.hpp>
#include <termin/render/render_camera.hpp>
#include <termin/render/render_execution_capabilities.hpp>
#include <termin/render/render_export.hpp>

#include <tgfx2/descriptors.hpp>

namespace tgfx {
class RenderContext2;
}

namespace termin {

class RenderItemSnapshot;
struct FrameGraphCaptureRequest;

// Per-resource tgfx2 texture map. Passes that draw through ctx2
// consume entries from tex2_reads/tex2_writes (and the depth variants
// below) directly.
using Tex2Map = std::unordered_map<std::string, tgfx::TextureHandle>;

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
    // Non-texture framegraph resources used by this pass. The map contains
    // only declared reads/writes and preserves canonical aliases without
    // exposing any domain-specific resource type to the executor contract.
    ResourceMap frame_graph_resources;
    // Render extent for the pass. This is not a display viewport rectangle.
    Rect2i render_rect;
    RenderViewState view;
    std::string render_target_name;
    // Borrowed from RenderEngine for this scene/view execution. Immutable
    // after its first successful collection and shared by all geometry passes.
    const RenderItemSnapshot* render_item_snapshot = nullptr;
    // Adapter-owned typed services borrowed for this execution. Generic
    // execution forwards the registry without interpreting its contents.
    const RenderExecutionCapabilities* capabilities = nullptr;
    // Frame-local debugger requests for this pass. These pointers are valid
    // only until the enclosing RenderEngine execution returns.
    std::vector<FrameGraphCaptureRequest*> debug_internal_capture_requests;

    FrameGraphResource* get_frame_graph_resource(
        const std::string& name) const
    {
        auto it = frame_graph_resources.find(name);
        return it == frame_graph_resources.end() ? nullptr : it->second;
    }

    template <typename ResourceT>
    ResourceT* get_frame_graph_resource_as(const std::string& name) const
    {
        return dynamic_cast<ResourceT*>(get_frame_graph_resource(name));
    }

    RENDER_CORE_API const std::string* requested_internal_symbol() const;
    RENDER_CORE_API bool should_capture_internal(const char* symbol) const;
    RENDER_CORE_API bool capture_internal(
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
    RENDER_CORE_API bool build_render_pass(
        std::span<const FrameGraphColorAttachment> colors,
        const FrameGraphDepthAttachment* depth,
        tgfx::RenderPassDesc& out_pass
    ) const;
};

} // namespace termin
