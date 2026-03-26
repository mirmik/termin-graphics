#pragma once

#include <glad/glad.h>
#include "tgfx2/tgfx2_api.h"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"

namespace tgfx2 {

class TGFX2_API OpenGLCommandList : public ICommandList {
public:
    explicit OpenGLCommandList(OpenGLRenderDevice& device);
    ~OpenGLCommandList() override;

    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassDesc& pass) override;
    void end_render_pass() override;

    void bind_pipeline(PipelineHandle pipeline) override;
    void bind_resource_set(ResourceSetHandle set) override;

    void bind_vertex_buffer(uint32_t slot, BufferHandle buffer, uint64_t offset = 0) override;
    void bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset = 0) override;

    void draw(uint32_t vertex_count, uint32_t first_vertex = 0) override;
    void draw_indexed(uint32_t index_count, uint32_t first_index = 0, int32_t vertex_offset = 0) override;
    void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

    void copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                     uint64_t src_offset = 0, uint64_t dst_offset = 0) override;
    void copy_texture(TextureHandle src, TextureHandle dst) override;

    void set_viewport(int x, int y, int width, int height) override;
    void set_scissor(int x, int y, int width, int height) override;

private:
    OpenGLRenderDevice& device_;
    GLuint current_vao_ = 0;
    GLuint current_fbo_ = 0;
    PipelineHandle current_pipeline_;
    GLenum current_topology_ = GL_TRIANGLES;
    GLenum current_index_type_ = GL_UNSIGNED_INT;
    uint64_t current_index_offset_ = 0;
    bool in_render_pass_ = false;

    void setup_vao_for_pipeline(GLPipeline* pipeline);
    void rebind_vertex_attribs(const VertexBufferLayout& layout, uint64_t base_offset);
};

} // namespace tgfx2
