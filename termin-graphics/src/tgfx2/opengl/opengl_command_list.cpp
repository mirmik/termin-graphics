#include "tgfx2/opengl/opengl_command_list.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/opengl/opengl_type_conversions.hpp"

#include <algorithm>

// GL 4.5 enums missing from our glad-3.3 header.
#ifndef GL_UPPER_LEFT
#define GL_UPPER_LEFT 0x8CA2
#endif
#ifndef GL_ZERO_TO_ONE
#define GL_ZERO_TO_ONE 0x935F
#endif

// Platform-specific dynamic resolve for glClipControl. Matches the
// pattern in opengl_render_device.cpp so both call sites use the same
// loader. On Linux libGL exports the symbol natively.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace {
using PFN_glClipControl = void (APIENTRY *)(GLenum, GLenum);
PFN_glClipControl s_glClipControl = nullptr;
inline void reapply_clip_control_upper_left() {
    if (!s_glClipControl) {
        s_glClipControl = reinterpret_cast<PFN_glClipControl>(
            wglGetProcAddress("glClipControl"));
    }
    if (s_glClipControl) s_glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
}
}  // namespace
#else
extern "C" void glClipControl(GLenum origin, GLenum depth);
namespace {
inline void reapply_clip_control_upper_left() {
    glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
}
}  // namespace
#endif

namespace tgfx {

OpenGLCommandList::OpenGLCommandList(OpenGLRenderDevice& device)
    : device_(device) {}

OpenGLCommandList::~OpenGLCommandList() {
    if (current_vao_) {
        glDeleteVertexArrays(1, &current_vao_);
    }
}

void OpenGLCommandList::begin() {
    // Reset the device's push constants ring buffer offset. This is
    // conceptually "start of a new command recording" — we own the
    // ring for the duration of this command list's execution.
    device_.push_constants_reset_frame();
    pending_push_offset_ = 0;
    pending_push_size_ = 0;
}

void OpenGLCommandList::end() {
    // Nothing to do for immediate mode
}

// --- Render pass ---

void OpenGLCommandList::begin_render_pass(const RenderPassDesc& pass) {
    in_render_pass_ = true;

    // Bind FBO (0 = default framebuffer if no textures specified)
    current_fbo_ = device_.get_or_create_fbo(pass);
    glBindFramebuffer(GL_FRAMEBUFFER, current_fbo_);

    // Re-apply clip-control at the start of every pass. Our ortho
    // matrices (engine2d, text2d_renderer) assume y-down clip space
    // — OpenGL reaches that via glClipControl(UPPER_LEFT). The call
    // is issued once in OpenGLRenderDevice::ctor, but some hosts
    // reset it between frames: GLWpfControl's D3D9 shared-surface
    // interop in particular sheds GL state when more than one
    // control is alive in the same window, which otherwise flips
    // every tcplot panel upside-down. Issuing it here makes every
    // pass self-contained. Safe if the driver actually honoured the
    // first call (a no-op at negligible cost) and helps on drivers
    // where the first call's effect is lost.
    reapply_clip_control_upper_left();

    // Reset GL scissor — it is global state and survives across passes
    // and frames. If a previous pass (a UI widget with a begin_clip,
    // say) left scissor enabled with its widget's rect, the upcoming
    // glClear + draws in *this* pass would be clipped to that stale
    // rectangle. Disabling here gives every pass a clean slate; the
    // caller re-enables with set_scissor when it actually wants to
    // clip. Vulkan has no equivalent leak because scissor there is
    // part of the command buffer, not global state.
    glDisable(GL_SCISSOR_TEST);

    // Set viewport to match first color attachment size, or depth if no color
    if (!pass.colors.empty() && pass.colors[0].texture) {
        auto* tex = device_.get_texture(pass.colors[0].texture);
        if (tex) {
            glViewport(0, 0, tex->desc.width, tex->desc.height);
            cached_fb_height_ = static_cast<int>(tex->desc.height);
        }
    } else if (pass.has_depth && pass.depth.texture) {
        auto* tex = device_.get_texture(pass.depth.texture);
        if (tex) {
            glViewport(0, 0, tex->desc.width, tex->desc.height);
            cached_fb_height_ = static_cast<int>(tex->desc.height);
        }
    }

    // Clear based on load ops
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
        if (clear_mask & GL_DEPTH_BUFFER_BIT) {
            glDepthMask(GL_TRUE);
        }
        glClear(clear_mask);
    }
}

void OpenGLCommandList::end_render_pass() {
    // Restore default framebuffer
    if (current_fbo_ != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    current_fbo_ = 0;
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

    // Enable the attribute slots that the pipeline expects. We must NOT
    // call glVertexAttribPointer here: in the OpenGL core profile calling
    // that function without a bound GL_ARRAY_BUFFER raises
    // GL_INVALID_OPERATION and has no effect. bind_vertex_buffer() runs
    // after this with the VBO already bound and performs the actual
    // attribute-pointer configuration.
    for (const auto& layout : pipeline->desc.vertex_layouts) {
        for (const auto& attr : layout.attributes) {
            glEnableVertexAttribArray(attr.location);
            if (layout.per_instance) {
                glVertexAttribDivisor(attr.location, 1);
            }
        }
    }
}

// --- Push constants ---

void OpenGLCommandList::set_push_constants(const void* data, uint32_t size) {
    if (!data || size == 0) {
        pending_push_size_ = 0;
        return;
    }
    GLintptr offset = device_.push_constants_write(data, size);
    if (offset < 0) {
        // write failed or payload too large — skip binding, draw will
        // use whatever was previously bound (or nothing).
        pending_push_size_ = 0;
        return;
    }
    pending_push_offset_ = offset;
    pending_push_size_ = static_cast<GLsizeiptr>(size);
}

void OpenGLCommandList::apply_pending_push_constants() {
    if (pending_push_size_ == 0) return;
    GLuint ubo = device_.push_constants_ring_buffer();
    if (ubo == 0) return;
    glBindBufferRange(GL_UNIFORM_BUFFER, TGFX2_PUSH_CONSTANTS_BINDING, ubo,
                      pending_push_offset_, pending_push_size_);
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
    apply_pending_push_constants();
    glDrawArrays(current_topology_, first_vertex, vertex_count);
}

void OpenGLCommandList::draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) {
    if (current_vao_) glBindVertexArray(current_vao_);
    apply_pending_push_constants();

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

    // Attachment point + blit mask depend on whether this is colour
    // or depth/stencil. Previously we hard-wired COLOR_ATTACHMENT0 +
    // COLOR_BUFFER_BIT, which made the FBO incomplete for depth textures
    // and blit silently no-op'd — shadow-map captures in Frame Debugger
    // came back all zero.
    auto is_depth = [](PixelFormat f) {
        return f == PixelFormat::D24_UNorm ||
               f == PixelFormat::D24_UNorm_S8_UInt ||
               f == PixelFormat::D32F;
    };
    const bool depth_copy = is_depth(s->desc.format);
    const GLenum attach = depth_copy ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
    const GLbitfield bit = depth_copy ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;
    // Depth blits must be GL_NEAREST per spec; linear only valid for color.
    const GLenum filter = depth_copy ? GL_NEAREST : GL_NEAREST;

    // glBlitFramebuffer honours GL_SCISSOR_TEST and the color write mask.
    // Previous UI/clipping passes may leave scissor enabled with a tiny
    // rect — then blit copies only that rect and PostFX's color_pp shows
    // up as a diagonal sliver (or whatever leaked through). Save and
    // disable both, restore afterwards. glCopyImageSubData (GL 4.3)
    // would sidestep this entirely but glad in this project is loaded
    // for GL 4.1 only.
    GLboolean was_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean color_mask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    GLboolean depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

    if (was_scissor) glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    GLuint fbo_read = 0, fbo_draw = 0;
    glGenFramebuffers(1, &fbo_read);
    glGenFramebuffers(1, &fbo_draw);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_read);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, attach, s->target, s->gl_id, 0);
    if (!depth_copy) {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_draw);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attach, d->target, d->gl_id, 0);
    if (!depth_copy) {
        GLenum draw_buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &draw_buf);
    } else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    glBlitFramebuffer(0, 0, s->desc.width, s->desc.height,
                      0, 0, d->desc.width, d->desc.height,
                      bit, filter);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo_read);
    glDeleteFramebuffers(1, &fbo_draw);

    glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
    glDepthMask(depth_mask);
    if (was_scissor) glEnable(GL_SCISSOR_TEST);
}

// --- Dynamic state ---

// Caller contract for set_viewport/set_scissor is top-left origin
// pixel coords (matches Vulkan and tcgui). GL's glViewport / glScissor
// still use bottom-left origin in window coordinates — glClipControl
// changes the clip→window mapping only, not the viewport/scissor
// origin. We flip the y here using the framebuffer height recorded
// in begin_render_pass so callers can stay backend-agnostic.

void OpenGLCommandList::set_viewport(int x, int y, int width, int height) {
    const int gl_y = (cached_fb_height_ > 0)
        ? (cached_fb_height_ - (y + height))
        : y;
    glViewport(x, gl_y, width, height);
}

void OpenGLCommandList::set_scissor(int x, int y, int width, int height) {
    if (width == 0 && height == 0) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        const int gl_y = (cached_fb_height_ > 0)
            ? (cached_fb_height_ - (y + height))
            : y;
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, gl_y, width, height);
    }
}

} // namespace tgfx
