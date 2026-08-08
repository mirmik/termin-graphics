#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/render_state.hpp"
#include "tgfx2/vertex_layout.hpp"

namespace tgfx {

    // Portable engine limit for ordered color attachments. Backends may expose a
    // lower runtime limit through BackendCapabilities::max_color_attachments.
    // Keeping the pipeline identity bounded avoids heap allocation on every draw.
    constexpr uint32_t TGFX2_MAX_COLOR_ATTACHMENTS = 8;

    // --- Resource descriptors ---

    struct BufferDesc {
        uint64_t size = 0;
        BufferUsage usage{};
        bool cpu_visible = false;
        // Required when BufferUsage::Storage is consumed as a D3D11
        // StructuredBuffer SRV. Leave zero for non-structured buffers.
        uint32_t structured_stride = 0;
    };

    struct TextureDesc {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t mip_levels = 1;
        // Number of 2D array layers. A value greater than one is an explicit
        // layered resource; backends which do not advertise texture-array
        // support must reject it instead of silently creating a 2D texture.
        uint32_t array_layers = 1;
        uint32_t sample_count = 1;
        PixelFormat format = PixelFormat::RGBA8_UNorm;
        TextureUsage usage{};
    };

    enum class ExternalTextureState : uint8_t {
        Undefined,
        ColorAttachment,
        ShaderRead,
    };

    struct ExternalTextureAccessDesc {
        ExternalTextureState state_after_wait = ExternalTextureState::Undefined;
        ExternalTextureState required_before_release = ExternalTextureState::ColorAttachment;
    };

    struct SamplerDesc {
        FilterMode min_filter = FilterMode::Linear;
        FilterMode mag_filter = FilterMode::Linear;
        FilterMode mip_filter = FilterMode::Linear;
        AddressMode address_u = AddressMode::Repeat;
        AddressMode address_v = AddressMode::Repeat;
        AddressMode address_w = AddressMode::Repeat;
        float max_anisotropy = 1.0f;
        bool compare_enable = false;
        CompareOp compare_op = CompareOp::Never;
    };

    struct ShaderDesc {
        ShaderStage stage = ShaderStage::Vertex;
        std::string source;
        std::string entry_point = "main";
        std::string debug_name;
        // Prebuilt shader resource-layout sidecar. WebGPU consumes the version 3
        // JSON contract emitted beside WGSL; runtime shader compilation and
        // reflection are deliberately outside the browser backend.
        std::string resource_layout_json;
        // SPIR-V bytecode (for Vulkan path; empty for GL-only)
        std::vector<uint8_t> bytecode;
    };

    struct PipelineDesc {
        ShaderHandle vertex_shader;
        ShaderHandle fragment_shader;
        ShaderHandle geometry_shader; // optional (id=0 = not used)

        std::vector<VertexLayoutDesc> vertex_layouts;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;

        RasterState raster;
        DepthStencilState depth_stencil;
        BlendState blend;
        ColorMask color_mask;

        std::vector<PixelFormat> color_formats;
        PixelFormat depth_format = PixelFormat::D32F;
        uint32_t sample_count = 1;
        // Graphics pipelines are created against a compatible render pass.
        // Vulkan render-pass compatibility includes the multiview view mask, so
        // mono and multiview pipelines must remain distinct cache identities.
        uint32_t view_count = 1;
        // Bit N is set when color attachment N has a single-sample resolve
        // target in the render pass this pipeline is used with. Vulkan render
        // pass compatibility includes resolve attachment references.
        uint32_t color_resolve_mask = 0;
    };

    // --- Render pass ---

    struct ColorAttachmentDesc {
        TextureHandle texture;
        // Optional single-sample target resolved from texture when the
        // physical render pass ends. The source must be multisampled; source
        // and destination format, extent and layer count must match.
        TextureHandle resolve_texture;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::Store;
        float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct DepthAttachmentDesc {
        TextureHandle texture;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::Store;
        float clear_depth = 1.0f;
        uint8_t clear_stencil = 0;
    };

    struct RenderPassDesc {
        std::vector<ColorAttachmentDesc> colors;
        DepthAttachmentDesc depth;
        bool has_depth = false;
    };

    enum class MultiviewColorFinalState : uint8_t {
        ShaderRead,
        ColorAttachment,
    };

    // Explicit multiview contract. It deliberately is not a flag on
    // RenderPassDesc: callers choosing this descriptor promise one invocation
    // which broadcasts every draw to view_count attachment layers.
    struct MultiviewRenderPassDesc {
        std::vector<ColorAttachmentDesc> colors;
        DepthAttachmentDesc depth;
        bool has_depth = false;
        uint32_t view_count = 2;
        MultiviewColorFinalState color_final_state = MultiviewColorFinalState::ShaderRead;
    };

} // namespace tgfx
