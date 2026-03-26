#pragma once

#include <cstdint>
#include "tgfx2/handles.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"

namespace tgfx2 {

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

} // namespace tgfx2
