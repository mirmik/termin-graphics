#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/render_state.hpp"
#include "tgfx2/vertex_layout.hpp"

namespace tgfx {

// --- Resource descriptors ---

struct BufferDesc {
    uint64_t size = 0;
    BufferUsage usage{};
    bool cpu_visible = false;
};

struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mip_levels = 1;
    uint32_t sample_count = 1;
    PixelFormat format = PixelFormat::RGBA8_UNorm;
    TextureUsage usage{};
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
    // SPIR-V bytecode (for Vulkan path; empty for GL-only)
    std::vector<uint8_t> bytecode;
};

struct PipelineDesc {
    ShaderHandle vertex_shader;
    ShaderHandle fragment_shader;
    ShaderHandle geometry_shader;  // optional (id=0 = not used)

    std::vector<VertexBufferLayout> vertex_layouts;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    RasterState raster;
    DepthStencilState depth_stencil;
    BlendState blend;
    ColorMask color_mask;

    std::vector<PixelFormat> color_formats;
    PixelFormat depth_format = PixelFormat::D32F;
    uint32_t sample_count = 1;
};

// --- Render pass ---

struct ColorAttachmentDesc {
    TextureHandle texture;
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

// --- Resource binding ---

struct ResourceBinding {
    uint32_t set = 0;
    uint32_t binding = 0;
    enum class Kind {
        UniformBuffer, StorageBuffer, SampledTexture, Sampler,
    } kind = Kind::UniformBuffer;
    BufferHandle buffer;
    TextureHandle texture;
    SamplerHandle sampler;
    uint64_t offset = 0;
    uint64_t range = 0;
};

struct ResourceSetDesc {
    std::vector<ResourceBinding> bindings;
};

} // namespace tgfx
