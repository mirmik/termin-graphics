#pragma once

#include <tgfx/frame_graph_resource.hpp>
#include <tgfx2/handles.hpp>

#include <termin/render/render_export.hpp>
#include <termin/render/resource_spec.hpp>

namespace termin {

using FrameGraphResourceCreateFn = FrameGraphResource* (*)(
    const ResourceSpec& spec);
enum class FrameGraphResourceSampledTextureKind {
    Color,
    Depth,
};

struct FrameGraphResourceSampledTexture {
    tgfx::TextureHandle texture;
    FrameGraphResourceSampledTextureKind kind =
        FrameGraphResourceSampledTextureKind::Color;
};

using FrameGraphResourceSampledTextureFn = FrameGraphResourceSampledTexture (*)(
    const FrameGraphResource& resource);

// Registration is a cold-path extension boundary for non-texture framegraph
// resources. The registry owns the type name, while factories return one
// heap-allocated resource whose lifetime is transferred to PipelineRenderCache.
struct FrameGraphResourceTypeDescriptor {
    const char* resource_type = nullptr;
    FrameGraphResourceCreateFn create = nullptr;
    FrameGraphResourceSampledTextureFn sampled_texture = nullptr;
};

RENDER_API bool register_frame_graph_resource_type(
    const FrameGraphResourceTypeDescriptor& descriptor);
RENDER_API bool unregister_frame_graph_resource_type(
    const char* resource_type);
RENDER_API bool has_frame_graph_resource_type(
    const char* resource_type);
RENDER_API bool frame_graph_resource_type_matches(
    const FrameGraphResourceTypeDescriptor& descriptor);
RENDER_API void clear_frame_graph_resource_types();

// Returns an owned resource or nullptr after logging a precise registry or
// factory error. Callers must not silently reinterpret unknown kinds as FBOs.
RENDER_API FrameGraphResource* create_frame_graph_resource(
    const ResourceSpec& spec);

// Optional generic sampled view used by ordinary texture consumers and the
// framegraph debugger. Its attachment kind preserves depth capture semantics;
// an empty handle means that the resource has no preview.
RENDER_API FrameGraphResourceSampledTexture frame_graph_resource_sampled_texture(
    const FrameGraphResource& resource);

} // namespace termin
