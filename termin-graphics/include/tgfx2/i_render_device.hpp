#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/handles.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/capabilities.hpp"
#include "tgfx2/i_command_list.hpp"

namespace tgfx2 {

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    virtual BackendCapabilities capabilities() const = 0;
    virtual void wait_idle() = 0;

    // --- Resource creation ---
    virtual BufferHandle create_buffer(const BufferDesc& desc) = 0;
    virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
    virtual SamplerHandle create_sampler(const SamplerDesc& desc) = 0;
    virtual ShaderHandle create_shader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;
    virtual ResourceSetHandle create_resource_set(const ResourceSetDesc& desc) = 0;

    // --- Resource destruction ---
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(PipelineHandle handle) = 0;
    virtual void destroy(ResourceSetHandle handle) = 0;

    // --- Data upload ---
    virtual void upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset = 0) = 0;
    virtual void upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip = 0) = 0;

    // --- Data readback ---
    virtual void read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset = 0) = 0;

    // --- Introspection ---
    // Query the descriptor (width/height/format/...) that a texture was
    // created or registered with. Returns a default-initialised
    // TextureDesc if the handle is invalid or unknown to this device.
    virtual TextureDesc texture_desc(TextureHandle handle) const = 0;

    // --- Command submission ---
    virtual std::unique_ptr<ICommandList> create_command_list(QueueType queue = QueueType::Graphics) = 0;
    virtual void submit(ICommandList& cmd) = 0;

    // --- Present / sync ---
    virtual void present() = 0;
};

} // namespace tgfx2
