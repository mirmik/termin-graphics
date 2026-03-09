# Backend-Neutral GPU API Draft

Дата: 2026-03-04

Черновик backend-agnostic API в стиле текущего `GraphicsBackend`, но с explicit семантикой (Vulkan/Metal/D3D12-first) и OpenGL как адаптером.

## Цели

1. Убрать GL-специфику из core-контрактов (`fbo_id`, `get_actual_gl_*`, `check_gl_error`, `init_opengl` и т.п.).
2. Зафиксировать переносимую модель: `Device + CommandList + Resource/Pipeline`.
3. Дать единый интерфейс для OpenGL/Vulkan/Metal/D3D12/Null backend.

## Черновой заголовок API

```cpp
// gpu_backend.hpp
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tgfx2 {

// ---------- Core enums ----------

enum class BackendType { OpenGL, Vulkan, Metal, D3D12, Null };
enum class QueueType { Graphics, Compute, Transfer };

enum class BufferUsage : uint32_t {
    Vertex = 1 << 0, Index = 1 << 1, Uniform = 1 << 2,
    Storage = 1 << 3, CopySrc = 1 << 4, CopyDst = 1 << 5
};

enum class TextureUsage : uint32_t {
    Sampled = 1 << 0, Storage = 1 << 1, ColorAttachment = 1 << 2,
    DepthStencilAttachment = 1 << 3, CopySrc = 1 << 4, CopyDst = 1 << 5
};

enum class PixelFormat {
    RGBA8_UNorm, BGRA8_UNorm, RG16F, RGBA16F, RGBA32F,
    D24S8, D32F
};

enum class LoadOp { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };

// ---------- Handles ----------

struct BufferHandle       { uint32_t id = 0; };
struct TextureHandle      { uint32_t id = 0; };
struct SamplerHandle      { uint32_t id = 0; };
struct ShaderHandle       { uint32_t id = 0; };
struct PipelineHandle     { uint32_t id = 0; };
struct ResourceSetHandle  { uint32_t id = 0; };
struct RenderTargetHandle { uint32_t id = 0; };
struct CommandListHandle  { uint32_t id = 0; };

// ---------- Capabilities ----------

struct BackendCapabilities {
    BackendType backend = BackendType::Null;
    bool supports_compute = false;
    bool supports_timestamp_queries = false;
    bool supports_multisample_resolve = true;
    uint32_t max_color_attachments = 4;
    uint32_t max_texture_dimension_2d = 8192;
};

// ---------- Resource desc ----------

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
    // min/mag/mipmap/filter/address modes
};

struct ShaderDesc {
    // Backend-neutral shader module input.
    // Example: SPIR-V + optional source for GL compatibility path.
    std::span<const uint8_t> bytecode;
    std::string entry_point = "main";
};

struct PipelineDesc {
    ShaderHandle vertex;
    ShaderHandle fragment;
    // blend/depth/raster state + vertex layout + render target formats
};

// ---------- Pass desc ----------

struct ColorAttachmentDesc {
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    float clear[4] = {0, 0, 0, 1};
};

struct DepthAttachmentDesc {
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    float clear_depth = 1.0f;
};

struct RenderPassDesc {
    std::vector<ColorAttachmentDesc> colors;
    DepthAttachmentDesc depth;
    bool has_depth = false;
};

// ---------- Binding model ----------

struct ResourceBinding {
    // set/binding style
    uint32_t set = 0;
    uint32_t binding = 0;
    enum class Kind { UniformBuffer, StorageBuffer, SampledTexture, Sampler } kind;
    BufferHandle buffer{};
    TextureHandle texture{};
    SamplerHandle sampler{};
    uint64_t offset = 0;
    uint64_t range = 0;
};

struct ResourceSetDesc {
    std::vector<ResourceBinding> bindings;
};

// ---------- Main interfaces ----------

class ICommandList {
public:
    virtual ~ICommandList() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void begin_render_pass(const RenderPassDesc& pass) = 0;
    virtual void end_render_pass() = 0;

    virtual void bind_pipeline(PipelineHandle pipeline) = 0;
    virtual void bind_resources(ResourceSetHandle set) = 0;

    virtual void bind_vertex_buffer(BufferHandle vb, uint64_t offset = 0) = 0;
    virtual void bind_index_buffer(BufferHandle ib, uint64_t offset = 0) = 0;

    virtual void draw(uint32_t vertex_count, uint32_t first_vertex = 0) = 0;
    virtual void draw_indexed(uint32_t index_count, uint32_t first_index = 0, int32_t vertex_offset = 0) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    virtual void copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size) = 0;
    virtual void copy_texture(TextureHandle src, TextureHandle dst) = 0;
};

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    virtual BackendCapabilities capabilities() const = 0;
    virtual void wait_idle() = 0;

    virtual BufferHandle create_buffer(const BufferDesc&) = 0;
    virtual TextureHandle create_texture(const TextureDesc&) = 0;
    virtual SamplerHandle create_sampler(const SamplerDesc&) = 0;
    virtual ShaderHandle create_shader(const ShaderDesc&) = 0;
    virtual PipelineHandle create_pipeline(const PipelineDesc&) = 0;
    virtual ResourceSetHandle create_resource_set(const ResourceSetDesc&) = 0;

    virtual void destroy(BufferHandle) = 0;
    virtual void destroy(TextureHandle) = 0;
    virtual void destroy(SamplerHandle) = 0;
    virtual void destroy(ShaderHandle) = 0;
    virtual void destroy(PipelineHandle) = 0;
    virtual void destroy(ResourceSetHandle) = 0;

    virtual void upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset = 0) = 0;
    virtual void upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip = 0) = 0;

    virtual std::unique_ptr<ICommandList> create_command_list(QueueType queue = QueueType::Graphics) = 0;
    virtual void submit(ICommandList& cmd) = 0;
    virtual void present() = 0;
};

} // namespace tgfx2
```

## Семантические правила (обязательные)

1. Core API не содержит OpenGL-терминов.
2. Ресурсы и пайплайны создаются по explicit descriptor-ам.
3. Команды пишутся в `ICommandList`; глобального state machine в контракте нет.
4. Binding только slot/layout-based (`set/binding`), не uniform-by-name как базовая модель.
5. Backend-specific возможности публикуются через capability/extension слой, а не через загрязнение core API.

## Примечание по миграции

Этот draft совместим с фазовой миграцией:

1. Сначала вводится новый neutral-layer API.
2. OpenGL реализуется как compatibility backend-адаптер.
3. Затем подключаются Vulkan/Metal backends без изменения core-контракта.

