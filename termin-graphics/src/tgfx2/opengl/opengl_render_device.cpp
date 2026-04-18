#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/opengl/opengl_command_list.hpp"
#include "tgfx2/opengl/opengl_type_conversions.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/internal/shader_preprocess.hpp"

#include <stdexcept>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tcbase/tc_log.hpp>

extern "C" {
#include "tgfx/tgfx_resource_gpu.h"
}

namespace tgfx {

OpenGLRenderDevice::OpenGLRenderDevice() {
    // glad is a static library — each DLL/exe gets its own copy of function pointers.
    // We must load GL pointers within this DLL even if the caller already did so.
    if (!gladLoaderLoadGL()) {
        throw std::runtime_error("Failed to initialize OpenGL function pointers (glad)");
    }
    query_capabilities();
}

OpenGLRenderDevice::~OpenGLRenderDevice() {
    // Clean up cached FBOs
    for (auto& [key, fbo] : fbo_cache_) {
        if (fbo) glDeleteFramebuffers(1, &fbo);
    }
    fbo_cache_.clear();

    // Clean up push constants ring buffer
    if (push_ring_buf_) {
        glDeleteBuffers(1, &push_ring_buf_);
        push_ring_buf_ = 0;
    }

    // Clean up all remaining GL resources
    for (auto& [id, buf] : buffers_) {
        if (buf.gl_id) glDeleteBuffers(1, &buf.gl_id);
    }
    for (auto& [id, tex] : textures_) {
        if (tex.gl_id) glDeleteTextures(1, &tex.gl_id);
    }
    for (auto& [id, s] : samplers_) {
        if (s.gl_id) glDeleteSamplers(1, &s.gl_id);
    }
    for (auto& [id, sh] : shaders_) {
        if (sh.gl_shader) glDeleteShader(sh.gl_shader);
    }
    for (auto& [key, shared] : program_cache_) {
        if (shared.program) glDeleteProgram(shared.program);
    }
}

void OpenGLRenderDevice::query_capabilities() {
    caps_.backend = BackendType::OpenGL;

    GLint val;
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &val);
    caps_.max_color_attachments = static_cast<uint32_t>(val);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &val);
    caps_.max_texture_dimension_2d = static_cast<uint32_t>(val);

    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &val);
    caps_.max_texture_units = static_cast<uint32_t>(val);

    caps_.supports_compute = false; // Requires GL 4.3, not available with GL 3.3 glad
    caps_.supports_geometry_shaders = true;
    caps_.supports_timestamp_queries = (glQueryCounter != nullptr);
    caps_.supports_multisample_resolve = true;
}

BackendCapabilities OpenGLRenderDevice::capabilities() const {
    return caps_;
}

void OpenGLRenderDevice::wait_idle() {
    glFinish();
}

// --- Buffer ---

BufferHandle OpenGLRenderDevice::create_buffer(const BufferDesc& desc) {
    GLBuffer buf;
    buf.desc = desc;
    buf.target = gl::to_gl_buffer_target(desc.usage);

    glGenBuffers(1, &buf.gl_id);
    glBindBuffer(buf.target, buf.gl_id);
    glBufferData(buf.target, static_cast<GLsizeiptr>(desc.size), nullptr,
                 gl::to_gl_buffer_usage(desc.cpu_visible));
    glBindBuffer(buf.target, 0);

    return {buffers_.add(std::move(buf))};
}

// --- Texture ---

TextureHandle OpenGLRenderDevice::create_texture(const TextureDesc& desc) {
    GLTexture tex;
    tex.desc = desc;

    auto fmt = gl::to_gl_format(desc.format);

    if (desc.sample_count > 1) {
        tex.target = GL_TEXTURE_2D_MULTISAMPLE;
        glGenTextures(1, &tex.gl_id);
        glBindTexture(tex.target, tex.gl_id);
        glTexImage2DMultisample(tex.target, desc.sample_count, fmt.internal_format,
                                desc.width, desc.height, GL_TRUE);
    } else {
        tex.target = GL_TEXTURE_2D;
        glGenTextures(1, &tex.gl_id);
        glBindTexture(tex.target, tex.gl_id);
        // Allocate storage for all mip levels
        for (uint32_t mip = 0; mip < desc.mip_levels; ++mip) {
            uint32_t w = std::max(1u, desc.width >> mip);
            uint32_t h = std::max(1u, desc.height >> mip);
            glTexImage2D(tex.target, mip, fmt.internal_format, w, h, 0,
                         fmt.format, fmt.type, nullptr);
        }
        // Mandatory defaults: without these GL treats the texture as
        // incomplete (default MIN filter is NEAREST_MIPMAP_LINEAR,
        // which requires a full mip chain). Any `bind_sampled_texture`
        // call against an incomplete texture returns black.
        // Samplers bound at draw time override these per-unit, so
        // they're just a "valid baseline".
        glTexParameteri(tex.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(tex.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(tex.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(tex.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(tex.target, GL_TEXTURE_MAX_LEVEL, desc.mip_levels - 1);
    }

    glBindTexture(tex.target, 0);
    return {textures_.add(std::move(tex))};
}

// --- Sampler ---

SamplerHandle OpenGLRenderDevice::create_sampler(const SamplerDesc& desc) {
    GLSampler samp;
    glGenSamplers(1, &samp.gl_id);

    glSamplerParameteri(samp.gl_id, GL_TEXTURE_MAG_FILTER, gl::to_gl_filter(desc.mag_filter));
    glSamplerParameteri(samp.gl_id, GL_TEXTURE_MIN_FILTER,
                        gl::to_gl_min_filter(desc.min_filter, desc.mip_filter));
    glSamplerParameteri(samp.gl_id, GL_TEXTURE_WRAP_S, gl::to_gl_address_mode(desc.address_u));
    glSamplerParameteri(samp.gl_id, GL_TEXTURE_WRAP_T, gl::to_gl_address_mode(desc.address_v));
    glSamplerParameteri(samp.gl_id, GL_TEXTURE_WRAP_R, gl::to_gl_address_mode(desc.address_w));

    // GL_TEXTURE_MAX_ANISOTROPY requires GL_EXT_texture_filter_anisotropic
    // Constant 0x84FE is the universally-accepted value
    if (desc.max_anisotropy > 1.0f) {
        glSamplerParameterf(samp.gl_id, 0x84FE, desc.max_anisotropy);
    }

    if (desc.compare_enable) {
        glSamplerParameteri(samp.gl_id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(samp.gl_id, GL_TEXTURE_COMPARE_FUNC, gl::to_gl_compare(desc.compare_op));
    }

    return {samplers_.add(std::move(samp))};
}

// --- Shader ---

ShaderHandle OpenGLRenderDevice::create_shader(const ShaderDesc& desc) {
    if (desc.source.empty()) {
        return {0};
    }

    GLShaderModule mod;
    mod.stage = desc.stage;

    GLenum gl_stage = gl::to_gl_shader_stage(desc.stage);
    mod.gl_shader = glCreateShader(gl_stage);

    // Resolve #include / @features etc. via the shared preprocessor
    // hook (see tgfx2/internal/shader_preprocess.hpp). Vulkan's
    // create_shader runs the exact same step so shaders line up
    // across backends.
    std::string resolved = internal::preprocess_shader_source(desc.source);
    const char* src = resolved.c_str();

    glShaderSource(mod.gl_shader, 1, &src, nullptr);
    glCompileShader(mod.gl_shader);

    GLint status;
    glGetShaderiv(mod.gl_shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetShaderInfoLog(mod.gl_shader, sizeof(log), nullptr, log);
        glDeleteShader(mod.gl_shader);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }

    return {shaders_.add(std::move(mod))};
}

// --- Pipeline ---

GLuint OpenGLRenderDevice::acquire_program(const PipelineDesc& desc) {
    GLProgramKey key{
        desc.vertex_shader.id,
        desc.fragment_shader.id,
        desc.geometry_shader.id,
    };
    auto it = program_cache_.find(key);
    if (it != program_cache_.end()) {
        it->second.ref_count += 1;
        return it->second.program;
    }

    auto* vs = get_shader(desc.vertex_shader);
    auto* fs = get_shader(desc.fragment_shader);
    if (!vs || !fs) {
        throw std::runtime_error("Pipeline requires valid vertex and fragment shaders");
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs->gl_shader);
    glAttachShader(program, fs->gl_shader);

    if (desc.geometry_shader && desc.geometry_shader.id != 0) {
        auto* gs = get_shader(desc.geometry_shader);
        if (gs) {
            glAttachShader(program, gs->gl_shader);
        }
    }

    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(std::string("Program link error: ") + log);
    }

    program_cache_[key] = GLSharedProgram{program, 1};
    program_to_key_[program] = key;
    return program;
}

void OpenGLRenderDevice::release_program(GLuint program) {
    if (program == 0) return;
    auto key_it = program_to_key_.find(program);
    if (key_it == program_to_key_.end()) {
        glDeleteProgram(program);
        return;
    }
    auto cache_it = program_cache_.find(key_it->second);
    if (cache_it == program_cache_.end()) {
        program_to_key_.erase(key_it);
        glDeleteProgram(program);
        return;
    }
    if (cache_it->second.ref_count > 1) {
        cache_it->second.ref_count -= 1;
        return;
    }
    glDeleteProgram(cache_it->second.program);
    program_cache_.erase(cache_it);
    program_to_key_.erase(key_it);
}

PipelineHandle OpenGLRenderDevice::create_pipeline(const PipelineDesc& desc) {
    GLPipeline pipe;
    pipe.desc = desc;
    pipe.program = acquire_program(desc);
    return {pipelines_.add(std::move(pipe))};
}

// --- Resource set ---

ResourceSetHandle OpenGLRenderDevice::create_resource_set(const ResourceSetDesc& desc) {
    GLResourceSet rs;
    rs.desc = desc;
    return {resource_sets_.add(std::move(rs))};
}

// --- Destroy ---

void OpenGLRenderDevice::destroy(BufferHandle handle) {
    if (auto* buf = buffers_.get(handle.id)) {
        if (buf->gl_id && !buf->external) glDeleteBuffers(1, &buf->gl_id);
        buffers_.remove(handle.id);
    }
}

void OpenGLRenderDevice::destroy(TextureHandle handle) {
    if (auto* tex = textures_.get(handle.id)) {
        const GLuint deleted_gl_id = tex->gl_id;

        if (tex->gl_id && !tex->external) glDeleteTextures(1, &tex->gl_id);
        textures_.remove(handle.id);

        // Drop any FBO cache entry that referenced this GL texture id.
        // Without this, a subsequent resize (PlotView*::ensure_offscreen_)
        // destroys the old color/depth textures, creates new ones, and
        // begin_pass may hit the old FBO cache entry — whose
        // glFramebufferTexture2D attachment still points at the
        // now-deleted texture object. OpenGL then silently renders into
        // nothing (black screen) because the FBO is effectively
        // incomplete or its attachments are dangling.
        //
        // External (non-owning) textures go through the same path —
        // the GL object is preserved but any FBO attached to its id is
        // still stale from tgfx2's point of view.
        if (deleted_gl_id != 0) {
            for (auto it = fbo_cache_.begin(); it != fbo_cache_.end(); ) {
                bool match = false;
                for (const auto& [attach_point, tex_id] : it->first) {
                    if (tex_id == deleted_gl_id) {
                        match = true;
                        break;
                    }
                }
                if (match) {
                    if (it->second != 0) glDeleteFramebuffers(1, &it->second);
                    it = fbo_cache_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}

TextureHandle OpenGLRenderDevice::register_external_texture(
    uintptr_t native_handle, const TextureDesc& desc) {
    GLTexture tex;
    tex.gl_id = static_cast<GLuint>(native_handle);
    tex.desc = desc;
    // Pick the right GL target based on sample count: multisample textures
    // must be attached with GL_TEXTURE_2D_MULTISAMPLE, or glFramebufferTexture2D
    // rejects the attachment and the FBO ends up INCOMPLETE_MISSING_ATTACHMENT.
    tex.target = (desc.sample_count > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    tex.external = true;
    TextureHandle h;
    h.id = textures_.add(std::move(tex));
    return h;
}

BufferHandle OpenGLRenderDevice::register_external_buffer(
    uintptr_t native_handle, const BufferDesc& desc) {
    GLBuffer buf;
    buf.gl_id = static_cast<GLuint>(native_handle);
    buf.desc = desc;
    buf.target = gl::to_gl_buffer_target(desc.usage);
    buf.external = true;
    BufferHandle h;
    h.id = buffers_.add(std::move(buf));
    return h;
}

void OpenGLRenderDevice::destroy(SamplerHandle handle) {
    if (auto* s = samplers_.get(handle.id)) {
        if (s->gl_id) glDeleteSamplers(1, &s->gl_id);
        samplers_.remove(handle.id);
    }
}

void OpenGLRenderDevice::destroy(ShaderHandle handle) {
    if (auto* sh = shaders_.get(handle.id)) {
        if (sh->gl_shader) glDeleteShader(sh->gl_shader);
        shaders_.remove(handle.id);
    }
}

void OpenGLRenderDevice::destroy(PipelineHandle handle) {
    if (auto* p = pipelines_.get(handle.id)) {
        release_program(p->program);
        pipelines_.remove(handle.id);
    }
}

void OpenGLRenderDevice::destroy(ResourceSetHandle handle) {
    resource_sets_.remove(handle.id);
}

// --- Upload ---

void OpenGLRenderDevice::upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset) {
    auto* buf = buffers_.get(dst.id);
    if (!buf) return;

    glBindBuffer(buf->target, buf->gl_id);
    glBufferSubData(buf->target, static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(data.size()), data.data());
    glBindBuffer(buf->target, 0);
}

void OpenGLRenderDevice::upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip) {
    auto* tex = textures_.get(dst.id);
    if (!tex) return;

    auto fmt = gl::to_gl_format(tex->desc.format);
    uint32_t w = std::max(1u, tex->desc.width >> mip);
    uint32_t h = std::max(1u, tex->desc.height >> mip);

    glBindTexture(tex->target, tex->gl_id);
    glTexSubImage2D(tex->target, mip, 0, 0, w, h, fmt.format, fmt.type, data.data());
    glBindTexture(tex->target, 0);
}

void OpenGLRenderDevice::upload_texture_region(TextureHandle dst,
                                               uint32_t x, uint32_t y,
                                               uint32_t w, uint32_t h,
                                               std::span<const uint8_t> data,
                                               uint32_t mip) {
    auto* tex = textures_.get(dst.id);
    if (!tex) return;

    auto fmt = gl::to_gl_format(tex->desc.format);

    glBindTexture(tex->target, tex->gl_id);
    glTexSubImage2D(tex->target, mip,
                    static_cast<GLint>(x), static_cast<GLint>(y),
                    static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                    fmt.format, fmt.type, data.data());
    glBindTexture(tex->target, 0);
}

// --- Readback ---

void OpenGLRenderDevice::read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset) {
    auto* buf = buffers_.get(src.id);
    if (!buf) return;

    glBindBuffer(buf->target, buf->gl_id);
    void* mapped = glMapBufferRange(buf->target, static_cast<GLintptr>(offset),
                                     static_cast<GLsizeiptr>(data.size()), GL_MAP_READ_BIT);
    if (mapped) {
        std::memcpy(data.data(), mapped, data.size());
        glUnmapBuffer(buf->target);
    }
    glBindBuffer(buf->target, 0);
}

TextureDesc OpenGLRenderDevice::texture_desc(TextureHandle handle) const {
    auto it = textures_.get_const(handle.id);
    if (!it) return {};
    return it->desc;
}

// --- Command list ---

std::unique_ptr<ICommandList> OpenGLRenderDevice::create_command_list(QueueType /*queue*/) {
    return std::make_unique<OpenGLCommandList>(*this);
}

void OpenGLRenderDevice::submit(ICommandList& /*cmd*/) {
    // Immediate mode: commands already executed. Flush for safety.
    glFlush();
}

void OpenGLRenderDevice::present() {
    // Swap buffer is handled by the windowing system (GLFW, Qt, etc.)
    glFlush();
}

// --- FBO cache ---

GLuint OpenGLRenderDevice::get_or_create_fbo(const RenderPassDesc& pass) {
    // Build cache key from attachment textures
    FBOKey key;

    for (size_t i = 0; i < pass.colors.size(); ++i) {
        auto* tex = get_texture(pass.colors[i].texture);
        if (tex) {
            key.emplace_back(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i), tex->gl_id);
        }
    }
    if (pass.has_depth) {
        auto* tex = get_texture(pass.depth.texture);
        if (tex) {
            key.emplace_back(GL_DEPTH_ATTACHMENT, tex->gl_id);
        }
    }

    // No textures attached = render to default framebuffer
    if (key.empty()) {
        return 0;
    }

    // Check cache
    auto it = fbo_cache_.find(key);
    if (it != fbo_cache_.end()) {
        return it->second;
    }

    // Create new FBO
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    bool has_color = false;
    for (size_t i = 0; i < pass.colors.size(); ++i) {
        auto* tex = get_texture(pass.colors[i].texture);
        if (tex) {
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                                   tex->target, tex->gl_id, 0);
            has_color = true;
        }
    }

    if (pass.has_depth) {
        auto* tex = get_texture(pass.depth.texture);
        if (tex) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   tex->target, tex->gl_id, 0);
        }
    }

    // Depth-only FBO (e.g. shadow map): disable color read/write
    if (!has_color) {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        // Diagnostic dump: which attachments were actually bound.
        std::string detail;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "status=0x%04X colors=%zu has_depth=%d has_color_attached=%d",
                      status, pass.colors.size(), pass.has_depth ? 1 : 0, has_color ? 1 : 0);
        detail = buf;
        for (size_t i = 0; i < pass.colors.size(); ++i) {
            auto* t = get_texture(pass.colors[i].texture);
            std::snprintf(buf, sizeof(buf), " color[%zu]={handle_id=%u tex=%s",
                          i, pass.colors[i].texture.id, t ? "found" : "NULL");
            detail += buf;
            if (t) {
                std::snprintf(buf, sizeof(buf), " gl_id=%u target=0x%04X %ux%u samples=%u}",
                              t->gl_id, t->target, t->desc.width, t->desc.height, t->desc.sample_count);
                detail += buf;
            } else {
                detail += "}";
            }
        }
        if (pass.has_depth) {
            auto* t = get_texture(pass.depth.texture);
            std::snprintf(buf, sizeof(buf), " depth={handle_id=%u tex=%s",
                          pass.depth.texture.id, t ? "found" : "NULL");
            detail += buf;
            if (t) {
                std::snprintf(buf, sizeof(buf), " gl_id=%u target=0x%04X %ux%u samples=%u}",
                              t->gl_id, t->target, t->desc.width, t->desc.height, t->desc.sample_count);
                detail += buf;
            } else {
                detail += "}";
            }
        }
        glDeleteFramebuffers(1, &fbo);
        throw std::runtime_error("Framebuffer incomplete: " + detail);
    }

    fbo_cache_[key] = fbo;
    return fbo;
}

bool OpenGLRenderDevice::read_texture_rgba_float(
    TextureHandle tex, float* out
) {
    auto* t = get_texture(tex);
    if (!t || !out) return false;

    GLint prev_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           t->target, t->gl_id, 0);

    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0,
                     static_cast<GLsizei>(t->desc.width),
                     static_cast<GLsizei>(t->desc.height),
                     GL_RGBA, GL_FLOAT, out);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
    glDeleteFramebuffers(1, &fbo);
    return ok;
}

bool OpenGLRenderDevice::read_texture_depth_float(
    TextureHandle tex, float* out
) {
    auto* t = get_texture(tex);
    if (!t || !out) return false;

    GLint prev_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           t->target, t->gl_id, 0);
    // Depth-only FBO — disable color read/draw explicitly, otherwise
    // some drivers report incomplete.
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glReadPixels(0, 0,
                     static_cast<GLsizei>(t->desc.width),
                     static_cast<GLsizei>(t->desc.height),
                     GL_DEPTH_COMPONENT, GL_FLOAT, out);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
    glDeleteFramebuffers(1, &fbo);
    return ok;
}

bool OpenGLRenderDevice::read_pixel_rgba8(
    TextureHandle tex, int x, int y, float out_rgba[4]
) {
    auto* t = get_texture(tex);
    if (!t || !out_rgba) return false;

    GLint prev_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           t->target, t->gl_id, 0);

    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        uint8_t pixel[4] = {0, 0, 0, 0};
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        out_rgba[0] = pixel[0] / 255.0f;
        out_rgba[1] = pixel[1] / 255.0f;
        out_rgba[2] = pixel[2] / 255.0f;
        out_rgba[3] = pixel[3] / 255.0f;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
    glDeleteFramebuffers(1, &fbo);
    return ok;
}

void OpenGLRenderDevice::blit_to_external_target(
    uintptr_t dst,
    TextureHandle src_color,
    int src_x, int src_y, int src_w, int src_h,
    int dst_x, int dst_y, int dst_w, int dst_h
) {
    const GLuint dst_fbo_id = static_cast<GLuint>(dst);
    GLTexture* tex = textures_.get(src_color.id);
    if (!tex) return;

    // Preserve any FBO the caller had bound so we don't clobber their
    // state. After the blit the previous bindings are restored.
    GLint prev_read = 0, prev_draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

    // Temporary read FBO anchored at the source texture. A throwaway
    // is cheaper than a cache lookup here because the caller's
    // textures change size on viewport resize; cache hit rate would
    // be low.
    GLuint read_fbo = 0;
    glGenFramebuffers(1, &read_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           tex->target, tex->gl_id, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo_id);

    glBlitFramebuffer(
        src_x, src_y, src_x + src_w, src_y + src_h,
        dst_x, dst_y, dst_x + dst_w, dst_y + dst_h,
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw));
    glDeleteFramebuffers(1, &read_fbo);
}

void OpenGLRenderDevice::clear_external_target(
    uintptr_t dst,
    float r, float g, float b, float a,
    float depth,
    int viewport_x, int viewport_y,
    int viewport_w, int viewport_h
) {
    const GLuint dst_fbo_id = static_cast<GLuint>(dst);
    GLint prev_draw = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);
    GLint prev_vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo_id);
    glViewport(viewport_x, viewport_y, viewport_w, viewport_h);
    glClearColor(r, g, b, a);
    glClearDepth(depth);
    // Ensure writes aren't masked by prior shader state; save/restore
    // the mask bits so we don't override caller intent.
    GLboolean prev_depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);
    glDepthMask(GL_TRUE);
    GLboolean prev_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColorMask(prev_color_mask[0], prev_color_mask[1],
                prev_color_mask[2], prev_color_mask[3]);
    glDepthMask(prev_depth_mask);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw));
}

void OpenGLRenderDevice::blit_to_texture(
    TextureHandle dst_color,
    TextureHandle src_color,
    int src_x, int src_y, int src_w, int src_h,
    int dst_x, int dst_y, int dst_w, int dst_h
) {
    GLTexture* src = textures_.get(src_color.id);
    GLTexture* dst = textures_.get(dst_color.id);
    if (!src || !dst) return;

    GLint prev_read = 0, prev_draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

    GLuint fbos[2] = {0, 0};
    glGenFramebuffers(2, fbos);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           src->target, src->gl_id, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           dst->target, dst->gl_id, 0);

    glBlitFramebuffer(
        src_x, src_y, src_x + src_w, src_y + src_h,
        dst_x, dst_y, dst_x + dst_w, dst_y + dst_h,
        GL_COLOR_BUFFER_BIT, GL_LINEAR
    );

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw));
    glDeleteFramebuffers(2, fbos);
}

void OpenGLRenderDevice::clear_texture(
    TextureHandle dst_color,
    float r, float g, float b, float a,
    int viewport_x, int viewport_y,
    int viewport_w, int viewport_h
) {
    GLTexture* dst = textures_.get(dst_color.id);
    if (!dst) return;

    GLint prev_draw = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);
    GLint prev_vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           dst->target, dst->gl_id, 0);
    glViewport(viewport_x, viewport_y, viewport_w, viewport_h);
    glClearColor(r, g, b, a);
    GLboolean prev_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glClear(GL_COLOR_BUFFER_BIT);

    glColorMask(prev_color_mask[0], prev_color_mask[1],
                prev_color_mask[2], prev_color_mask[3]);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw));
    glDeleteFramebuffers(1, &fbo);
}

uintptr_t OpenGLRenderDevice::native_texture_handle(TextureHandle handle) const {
    // HandlePool::get is non-const; safe to const_cast for read-only lookup.
    auto* tex = const_cast<OpenGLRenderDevice*>(this)->textures_.get(handle.id);
    return tex ? static_cast<uintptr_t>(tex->gl_id) : 0;
}

void OpenGLRenderDevice::reset_state() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    glDisable(GL_BLEND);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
}

void OpenGLRenderDevice::flush() { glFlush(); }
void OpenGLRenderDevice::finish() { glFinish(); }

void OpenGLRenderDevice::invalidate_fbo_cache() {
    for (auto& [key, fbo] : fbo_cache_) {
        if (fbo != 0) {
            glDeleteFramebuffers(1, &fbo);
        }
    }
    fbo_cache_.clear();
}

// --- Push constants ring buffer ---

void OpenGLRenderDevice::ensure_push_ring() {
    if (push_ring_initialized_) return;

    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &push_ring_alignment_);
    if (push_ring_alignment_ <= 0) {
        push_ring_alignment_ = 256;
    }

    glGenBuffers(1, &push_ring_buf_);
    glBindBuffer(GL_UNIFORM_BUFFER, push_ring_buf_);
    // GL_STREAM_DRAW hints "written once, used a few times, re-written".
    // Paired with buffer orphaning (glBufferData NULL on overflow) this
    // avoids GPU stalls on reuse.
    glBufferData(GL_UNIFORM_BUFFER, push_ring_size_, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    push_ring_offset_ = 0;
    push_ring_initialized_ = true;
}

void OpenGLRenderDevice::push_constants_reset_frame() {
    push_ring_offset_ = 0;
}

GLintptr OpenGLRenderDevice::push_constants_write(const void* data, uint32_t size) {
    if (!data || size == 0) {
        return -1;
    }
    if (size > TGFX2_PUSH_CONSTANTS_MAX_BYTES) {
        tc::Log::error("tgfx2: push constants payload %u bytes exceeds max %u",
                       size, TGFX2_PUSH_CONSTANTS_MAX_BYTES);
        return -1;
    }

    ensure_push_ring();

    const GLintptr align  = static_cast<GLintptr>(push_ring_alignment_);
    const GLintptr padded = (static_cast<GLintptr>(size) + align - 1) / align * align;

    // Align current offset to the UBO alignment requirement.
    GLintptr offset = (push_ring_offset_ + align - 1) / align * align;

    if (offset + padded > push_ring_size_) {
        // Ring overflow: orphan the buffer storage so the driver gives
        // us a fresh GPU allocation without stalling on old contents,
        // then rewind the write cursor. This is the "invalidate the
        // whole buffer" idiom (equivalent to glInvalidateBufferData on
        // GL 4.3+).
        glBindBuffer(GL_UNIFORM_BUFFER, push_ring_buf_);
        glBufferData(GL_UNIFORM_BUFFER, push_ring_size_, nullptr, GL_STREAM_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        offset = 0;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, push_ring_buf_);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, static_cast<GLsizeiptr>(size), data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    push_ring_offset_ = offset + padded;
    return offset;
}

} // namespace tgfx
