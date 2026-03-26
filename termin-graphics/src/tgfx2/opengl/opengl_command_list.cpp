#include "tgfx2/opengl/opengl_command_list.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/opengl/opengl_type_conversions.hpp"

namespace tgfx2 {

OpenGLCommandList::OpenGLCommandList(OpenGLRenderDevice& device)
    : device_(device) {}

OpenGLCommandList::~OpenGLCommandList() {
    if (current_vao_) {
        glDeleteVertexArrays(1, &current_vao_);
    }
}

void OpenGLCommandList::begin() {
    // Nothing to do for immediate mode
}

void OpenGLCommandList::end() {
    // Nothing to do for immediate mode
}

// --- Render pass ---

void OpenGLCommandList::begin_render_pass(const RenderPassDesc& pass) {
    in_render_pass_ = true;

    // For now: bind default framebuffer or FBO based on attachments.
    // Full FBO management will be added when render targets are implemented.
    // Currently we clear based on load ops.

    GLbitfield clear_mask = 0;

    for (const auto& color : pass.colors) {
        if (color.load == LoadOp::Clear) {
            glClearColor(color.clear_color[0], color.clear_color[1],
                         color.clear_color[2], color.clear_color[3]);
            clear_mask |= GL_COLOR_BUFFER_BIT;
        }
    }

    if (pass.has_depth) {
        if (pass.depth.load == LoadOp::Clear) {
            glClearDepth(pass.depth.clear_depth);
            clear_mask |= GL_DEPTH_BUFFER_BIT;
        }
    }

    if (clear_mask) {
        // Ensure depth write is enabled for clear
        if (clear_mask & GL_DEPTH_BUFFER_BIT) {
            glDepthMask(GL_TRUE);
        }
        glClear(clear_mask);
    }
}

void OpenGLCommandList::end_render_pass() {
    in_render_pass_ = false;
}

// --- Pipeline ---

void OpenGLCommandList::bind_pipeline(PipelineHandle pipeline) {
    auto* pipe = device_.get_pipeline(pipeline);
    if (!pipe) return;

    current_pipeline_ = pipeline;
    current_topology_ = gl::to_gl_topology(pipe->desc.topology);

    // Shader program
    glUseProgram(pipe->program);

    // Raster state
    const auto& raster = pipe->desc.raster;
    if (raster.cull != CullMode::None) {
        glEnable(GL_CULL_FACE);
        glCullFace(gl::to_gl_cull_mode(raster.cull));
    } else {
        glDisable(GL_CULL_FACE);
    }
    glFrontFace(gl::to_gl_front_face(raster.front_face));
    glPolygonMode(GL_FRONT_AND_BACK, gl::to_gl_polygon_mode(raster.polygon_mode));

    // Depth state
    const auto& ds = pipe->desc.depth_stencil;
    if (ds.depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(gl::to_gl_compare(ds.depth_compare));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(ds.depth_write ? GL_TRUE : GL_FALSE);

    // Blend state
    const auto& blend = pipe->desc.blend;
    if (blend.enabled) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(
            gl::to_gl_blend_factor(blend.src_color),
            gl::to_gl_blend_factor(blend.dst_color),
            gl::to_gl_blend_factor(blend.src_alpha),
            gl::to_gl_blend_factor(blend.dst_alpha));
        glBlendEquationSeparate(
            gl::to_gl_blend_op(blend.color_op),
            gl::to_gl_blend_op(blend.alpha_op));
    } else {
        glDisable(GL_BLEND);
    }

    // Color mask
    const auto& cm = pipe->desc.color_mask;
    glColorMask(cm.r, cm.g, cm.b, cm.a);

    // Set up VAO for vertex layout
    setup_vao_for_pipeline(pipe);
}

void OpenGLCommandList::setup_vao_for_pipeline(GLPipeline* pipeline) {
    if (current_vao_) {
        glDeleteVertexArrays(1, &current_vao_);
        current_vao_ = 0;
    }

    glGenVertexArrays(1, &current_vao_);
    glBindVertexArray(current_vao_);

    // Configure vertex attributes from layout (buffers bound later via bind_vertex_buffer)
    for (const auto& layout : pipeline->desc.vertex_layouts) {
        for (const auto& attr : layout.attributes) {
            glEnableVertexAttribArray(attr.location);

            auto gl_type = gl::vertex_format_gl_type(attr.format);
            auto count = gl::vertex_format_component_count(attr.format);

            if (gl::vertex_format_is_integer(attr.format)) {
                glVertexAttribIPointer(
                    attr.location, count, gl_type,
                    layout.stride,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset)));
            } else {
                glVertexAttribPointer(
                    attr.location, count, gl_type,
                    gl::vertex_format_is_normalized(attr.format) ? GL_TRUE : GL_FALSE,
                    layout.stride,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset)));
            }

            if (layout.per_instance) {
                glVertexAttribDivisor(attr.location, 1);
            }
        }
    }
}

// --- Resource binding ---

void OpenGLCommandList::bind_resource_set(ResourceSetHandle set) {
    auto* rs = device_.get_resource_set(set);
    if (!rs) return;

    for (const auto& b : rs->desc.bindings) {
        switch (b.kind) {
            case ResourceBinding::Kind::UniformBuffer: {
                auto* buf = device_.get_buffer(b.buffer);
                if (buf) {
                    if (b.range > 0) {
                        glBindBufferRange(GL_UNIFORM_BUFFER, b.binding, buf->gl_id,
                                          static_cast<GLintptr>(b.offset),
                                          static_cast<GLsizeiptr>(b.range));
                    } else {
                        glBindBufferBase(GL_UNIFORM_BUFFER, b.binding, buf->gl_id);
                    }
                }
                break;
            }
            case ResourceBinding::Kind::StorageBuffer: {
                auto* buf = device_.get_buffer(b.buffer);
                if (buf) {
                    if (b.range > 0) {
                        glBindBufferRange(0x90D2 /*GL_SHADER_STORAGE_BUFFER*/, b.binding, buf->gl_id,
                                          static_cast<GLintptr>(b.offset),
                                          static_cast<GLsizeiptr>(b.range));
                    } else {
                        glBindBufferBase(0x90D2 /*GL_SHADER_STORAGE_BUFFER*/, b.binding, buf->gl_id);
                    }
                }
                break;
            }
            case ResourceBinding::Kind::SampledTexture: {
                auto* tex = device_.get_texture(b.texture);
                if (tex) {
                    glActiveTexture(GL_TEXTURE0 + b.binding);
                    glBindTexture(tex->target, tex->gl_id);
                }
                break;
            }
            case ResourceBinding::Kind::Sampler: {
                auto* samp = device_.get_sampler(b.sampler);
                if (samp) {
                    glBindSampler(b.binding, samp->gl_id);
                }
                break;
            }
        }
    }
}

// --- Vertex / index buffers ---

void OpenGLCommandList::bind_vertex_buffer(uint32_t /*slot*/, BufferHandle buffer, uint64_t offset) {
    auto* buf = device_.get_buffer(buffer);
    if (!buf || !current_vao_) return;

    glBindVertexArray(current_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buf->gl_id);

    // Re-setup attribute pointers with the new buffer + offset
    auto* pipe = device_.get_pipeline(current_pipeline_);
    if (pipe && !pipe->desc.vertex_layouts.empty()) {
        const auto& layout = pipe->desc.vertex_layouts[0];
        for (const auto& attr : layout.attributes) {
            auto gl_type = gl::vertex_format_gl_type(attr.format);
            auto count = gl::vertex_format_component_count(attr.format);
            auto ptr_offset = static_cast<uintptr_t>(attr.offset + offset);

            if (gl::vertex_format_is_integer(attr.format)) {
                glVertexAttribIPointer(
                    attr.location, count, gl_type,
                    layout.stride,
                    reinterpret_cast<const void*>(ptr_offset));
            } else {
                glVertexAttribPointer(
                    attr.location, count, gl_type,
                    gl::vertex_format_is_normalized(attr.format) ? GL_TRUE : GL_FALSE,
                    layout.stride,
                    reinterpret_cast<const void*>(ptr_offset));
            }
        }
    }
}

void OpenGLCommandList::bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset) {
    auto* buf = device_.get_buffer(buffer);
    if (!buf || !current_vao_) return;

    current_index_type_ = gl::to_gl_index_type(type);
    current_index_offset_ = offset;

    glBindVertexArray(current_vao_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->gl_id);
}

// --- Draw ---

void OpenGLCommandList::draw(uint32_t vertex_count, uint32_t first_vertex) {
    if (current_vao_) glBindVertexArray(current_vao_);
    glDrawArrays(current_topology_, first_vertex, vertex_count);
}

void OpenGLCommandList::draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) {
    if (current_vao_) glBindVertexArray(current_vao_);

    auto index_size = (current_index_type_ == GL_UNSIGNED_SHORT) ? 2u : 4u;
    auto byte_offset = current_index_offset_ + first_index * index_size;
    auto* offset_ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(byte_offset));

    if (vertex_offset != 0) {
        glDrawElementsBaseVertex(current_topology_, index_count, current_index_type_,
                                 offset_ptr, vertex_offset);
    } else {
        glDrawElements(current_topology_, index_count, current_index_type_, offset_ptr);
    }
}

void OpenGLCommandList::dispatch(uint32_t /*group_x*/, uint32_t /*group_y*/, uint32_t /*group_z*/) {
    // Compute shaders require GL 4.3; not available with GL 3.3 glad
}

// --- Copy ---

void OpenGLCommandList::copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                                     uint64_t src_offset, uint64_t dst_offset) {
    auto* s = device_.get_buffer(src);
    auto* d = device_.get_buffer(dst);
    if (!s || !d) return;

    glBindBuffer(GL_COPY_READ_BUFFER, s->gl_id);
    glBindBuffer(GL_COPY_WRITE_BUFFER, d->gl_id);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                        static_cast<GLintptr>(src_offset),
                        static_cast<GLintptr>(dst_offset),
                        static_cast<GLsizeiptr>(size));
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void OpenGLCommandList::copy_texture(TextureHandle src, TextureHandle dst) {
    auto* s = device_.get_texture(src);
    auto* d = device_.get_texture(dst);
    if (!s || !d) return;

    // glCopyImageSubData requires GL 4.3; fallback via FBO blit
    GLuint fbo_read = 0, fbo_draw = 0;
    glGenFramebuffers(1, &fbo_read);
    glGenFramebuffers(1, &fbo_draw);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_read);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, s->target, s->gl_id, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_draw);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, d->target, d->gl_id, 0);

    glBlitFramebuffer(0, 0, s->desc.width, s->desc.height,
                      0, 0, d->desc.width, d->desc.height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo_read);
    glDeleteFramebuffers(1, &fbo_draw);
}

// --- Dynamic state ---

void OpenGLCommandList::set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void OpenGLCommandList::set_scissor(int x, int y, int width, int height) {
    if (width == 0 && height == 0) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, width, height);
    }
}

} // namespace tgfx2
