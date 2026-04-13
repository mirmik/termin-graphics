#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/opengl/opengl_command_list.hpp"
#include "tgfx2/opengl/opengl_type_conversions.hpp"

#include <stdexcept>
#include <string>
#include <cstdio>
#include <cstring>

namespace tgfx2 {

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
    for (auto& [id, p] : pipelines_) {
        if (p.program) glDeleteProgram(p.program);
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
        // Sensible defaults (can be overridden via sampler)
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

    const char* src = desc.source.c_str();
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

PipelineHandle OpenGLRenderDevice::create_pipeline(const PipelineDesc& desc) {
    GLPipeline pipe;
    pipe.desc = desc;

    // Link shader program
    auto* vs = get_shader(desc.vertex_shader);
    auto* fs = get_shader(desc.fragment_shader);
    if (!vs || !fs) {
        throw std::runtime_error("Pipeline requires valid vertex and fragment shaders");
    }

    pipe.program = glCreateProgram();
    glAttachShader(pipe.program, vs->gl_shader);
    glAttachShader(pipe.program, fs->gl_shader);

    if (desc.geometry_shader && desc.geometry_shader.id != 0) {
        auto* gs = get_shader(desc.geometry_shader);
        if (gs) {
            glAttachShader(pipe.program, gs->gl_shader);
        }
    }

    glLinkProgram(pipe.program);

    GLint status;
    glGetProgramiv(pipe.program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(pipe.program, sizeof(log), nullptr, log);
        glDeleteProgram(pipe.program);
        throw std::runtime_error(std::string("Program link error: ") + log);
    }

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
        if (buf->gl_id) glDeleteBuffers(1, &buf->gl_id);
        buffers_.remove(handle.id);
    }
}

void OpenGLRenderDevice::destroy(TextureHandle handle) {
    if (auto* tex = textures_.get(handle.id)) {
        if (tex->gl_id && !tex->external) glDeleteTextures(1, &tex->gl_id);
        textures_.remove(handle.id);
    }
}

TextureHandle OpenGLRenderDevice::register_external_texture(GLuint gl_id, const TextureDesc& desc) {
    GLTexture tex;
    tex.gl_id = gl_id;
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
        if (p->program) glDeleteProgram(p->program);
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

} // namespace tgfx2
