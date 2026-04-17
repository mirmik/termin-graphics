#pragma once

#include <cstdint>
#include "tgfx2/handles.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"

namespace tgfx {

// Reserved UBO binding slot used by the OpenGL backend to emulate
// push constants. Shaders that consume push constants must declare
// the block at this slot:
//
//   layout(std140, binding = 14) uniform PushConstants {
//       mat4 u_model;  // example per-object payload
//   };
//
// On Vulkan this slot is unused; push constants there go through
// VkPushConstantRange / vkCmdPushConstants and the `binding=14`
// qualifier is ignored (the glsl source is pre-processed to replace
// the UBO block with `layout(push_constant)` when the Vulkan backend
// compiles the shader — not implemented yet, Vulkan backend TBD).
//
// Maximum push constant payload is intentionally capped at 128 bytes
// to stay within the Vulkan minimum guaranteed push constant size
// (VkPhysicalDeviceLimits::maxPushConstantsSize == 128 on many
// mobile GPUs). Pass code must not exceed this.
constexpr uint32_t TGFX2_PUSH_CONSTANTS_BINDING = 14;
constexpr uint32_t TGFX2_PUSH_CONSTANTS_MAX_BYTES = 128;

class ICommandList {
public:
    virtual ~ICommandList() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    // Render pass
    virtual void begin_render_pass(const RenderPassDesc& pass) = 0;
    virtual void end_render_pass() = 0;

    // Pipeline & resources
    virtual void bind_pipeline(PipelineHandle pipeline) = 0;
    virtual void bind_resource_set(ResourceSetHandle set) = 0;

    // Push constants.
    //
    // Upload a small per-draw byte payload that can be read from
    // shaders via a push-constant block. On OpenGL this is backed by
    // a ring UBO at TGFX2_PUSH_CONSTANTS_BINDING; on Vulkan it maps
    // to vkCmdPushConstants.
    //
    // The payload is consumed by the *next* draw call — each draw
    // reads the most recently set push constants. Calling
    // set_push_constants() without a following draw is a no-op.
    //
    // `size` must be <= TGFX2_PUSH_CONSTANTS_MAX_BYTES. Larger payloads
    // belong in a regular uniform buffer.
    virtual void set_push_constants(const void* data, uint32_t size) = 0;

    // Vertex / index buffers
    virtual void bind_vertex_buffer(uint32_t slot, BufferHandle buffer, uint64_t offset = 0) = 0;
    virtual void bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset = 0) = 0;

    // Draw
    virtual void draw(uint32_t vertex_count, uint32_t first_vertex = 0) = 0;
    virtual void draw_indexed(uint32_t index_count, uint32_t first_index = 0, int32_t vertex_offset = 0) = 0;

    // Compute
    virtual void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) = 0;

    // Copy
    virtual void copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                             uint64_t src_offset = 0, uint64_t dst_offset = 0) = 0;
    virtual void copy_texture(TextureHandle src, TextureHandle dst) = 0;

    // Dynamic state
    virtual void set_viewport(int x, int y, int width, int height) = 0;
    virtual void set_scissor(int x, int y, int width, int height) = 0;
};

} // namespace tgfx
