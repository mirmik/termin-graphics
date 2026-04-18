#pragma once

#include <cstdint>

namespace tgfx {

enum class BackendType { OpenGL, Vulkan, Metal, D3D12, Null };
enum class QueueType { Graphics, Compute, Transfer };

// --- Buffer / Texture usage flags ---

enum class BufferUsage : uint32_t {
    Vertex   = 1 << 0,
    Index    = 1 << 1,
    Uniform  = 1 << 2,
    Storage  = 1 << 3,
    CopySrc  = 1 << 4,
    CopyDst  = 1 << 5,
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool has_flag(BufferUsage value, BufferUsage flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

enum class TextureUsage : uint32_t {
    Sampled              = 1 << 0,
    Storage              = 1 << 1,
    ColorAttachment      = 1 << 2,
    DepthStencilAttachment = 1 << 3,
    CopySrc              = 1 << 4,
    CopyDst              = 1 << 5,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool has_flag(TextureUsage value, TextureUsage flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// --- Pixel formats ---

enum class PixelFormat {
    R8_UNorm,
    RG8_UNorm,
    RGB8_UNorm,
    RGBA8_UNorm,
    BGRA8_UNorm,
    R16F,
    RG16F,
    RGBA16F,
    R32F,
    RG32F,
    RGBA32F,
    D24_UNorm,
    D24_UNorm_S8_UInt,
    D32F,
    // Sentinel: "no attachment of this kind". Used by RenderContext2
    // to tell the pipeline cache that the current pass has no depth
    // attachment, so the cached pipeline's VkRenderPass must be built
    // without one. Added at the end to preserve existing numeric values
    // of real formats.
    Undefined,
};

// --- Render pass ops ---

enum class LoadOp { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };

// --- Index type ---

enum class IndexType { Uint16, Uint32 };

// --- Shader stage ---

enum class ShaderStage { Vertex, Fragment, Geometry, Compute };

// --- Pipeline state enums ---

enum class CompareOp {
    Never, Less, Equal, LessEqual,
    Greater, NotEqual, GreaterEqual, Always,
};

enum class BlendFactor {
    Zero, One,
    SrcAlpha, OneMinusSrcAlpha,
    DstAlpha, OneMinusDstAlpha,
    SrcColor, OneMinusSrcColor,
    DstColor, OneMinusDstColor,
};

enum class BlendOp { Add, Subtract, ReverseSubtract, Min, Max };

enum class CullMode { None, Front, Back };
enum class FrontFace { CCW, CW };
enum class PolygonMode { Fill, Line, Point };
enum class PrimitiveTopology { PointList, LineList, LineStrip, TriangleList, TriangleStrip };

// --- Sampler ---

enum class FilterMode { Nearest, Linear };

enum class AddressMode {
    Repeat, MirroredRepeat, ClampToEdge, ClampToBorder,
};

} // namespace tgfx
