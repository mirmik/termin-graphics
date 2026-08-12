#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/opengl/opengl_command_list.hpp"
#include "tgfx2/opengl/gl_platform_operations.hpp"
#include "gl_web_compat.hpp"
#include "tgfx2/opengl/opengl_type_conversions.hpp"
#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tgfx2/tc_texture_upload.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tcbase/tc_log.h>
#include <tcbase/tc_log.hpp>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5_webgl.h>
extern "C" void* tgfx_webgl2_get_proc_address(const char* name);
#endif

extern "C" {
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_mesh_registry.h"
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
}

static void opengl_invalidate_tc_texture_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::OpenGLRenderDevice*>(user)->invalidate_tc_texture_cache(pool_index);
}

static void opengl_invalidate_tc_mesh_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::OpenGLRenderDevice*>(user)->invalidate_tc_mesh_cache(pool_index);
}

static void opengl_invalidate_tc_shader_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::OpenGLRenderDevice*>(user)->invalidate_tc_shader_cache(pool_index);
}

namespace tgfx {

    namespace {
        const char* gl_shader_stage_name(ShaderStage stage) {
            switch (stage) {
            case ShaderStage::Vertex:
                return "vertex";
            case ShaderStage::Fragment:
                return "fragment";
            case ShaderStage::Geometry:
                return "geometry";
            case ShaderStage::Compute:
                return "compute";
            }
            return "unknown";
        }
    } // namespace

    OpenGLRenderDevice::OpenGLRenderDevice(const OpenGLDeviceCreateInfo& info)
        : requested_tier_(info.feature_tier) {
        // glad is a static library — each DLL/exe gets its own copy of function pointers.
        // We must load GL pointers within this DLL even if the caller already did so.
#if defined(__EMSCRIPTEN__)
        if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(tgfx_webgl2_get_proc_address))) {
#else
        if (!gladLoaderLoadGL()) {
#endif
            throw std::runtime_error("Failed to initialize OpenGL function pointers (glad)");
        }
#if defined(__EMSCRIPTEN__)
        // GLAD is generated for desktop GL and sees WebGL's reported
        // "OpenGL ES 3.0" version as desktop 3.0. Several ES3-core entry
        // points are grouped under desktop GL 3.1-3.3 and therefore are not
        // requested automatically. Load that shared subset explicitly from
        // the static GLES bridge.
#define TGFX_LOAD_WEBGL2_GLAD(proc, type)                                                                              \
    glad_##proc = reinterpret_cast<type>(tgfx_webgl2_get_proc_address(#proc))
        TGFX_LOAD_WEBGL2_GLAD(glBindBufferBase, PFNGLBINDBUFFERBASEPROC);
        TGFX_LOAD_WEBGL2_GLAD(glBindBufferRange, PFNGLBINDBUFFERRANGEPROC);
        TGFX_LOAD_WEBGL2_GLAD(glBindSampler, PFNGLBINDSAMPLERPROC);
        TGFX_LOAD_WEBGL2_GLAD(glClientWaitSync, PFNGLCLIENTWAITSYNCPROC);
        TGFX_LOAD_WEBGL2_GLAD(glCopyBufferSubData, PFNGLCOPYBUFFERSUBDATAPROC);
        TGFX_LOAD_WEBGL2_GLAD(glDeleteSamplers, PFNGLDELETESAMPLERSPROC);
        TGFX_LOAD_WEBGL2_GLAD(glDeleteSync, PFNGLDELETESYNCPROC);
        TGFX_LOAD_WEBGL2_GLAD(glDrawArraysInstanced, PFNGLDRAWARRAYSINSTANCEDPROC);
        TGFX_LOAD_WEBGL2_GLAD(glDrawElementsInstanced, PFNGLDRAWELEMENTSINSTANCEDPROC);
        TGFX_LOAD_WEBGL2_GLAD(glFenceSync, PFNGLFENCESYNCPROC);
        TGFX_LOAD_WEBGL2_GLAD(glGenSamplers, PFNGLGENSAMPLERSPROC);
        TGFX_LOAD_WEBGL2_GLAD(glGetUniformBlockIndex, PFNGLGETUNIFORMBLOCKINDEXPROC);
        TGFX_LOAD_WEBGL2_GLAD(glSamplerParameterf, PFNGLSAMPLERPARAMETERFPROC);
        TGFX_LOAD_WEBGL2_GLAD(glSamplerParameteri, PFNGLSAMPLERPARAMETERIPROC);
        TGFX_LOAD_WEBGL2_GLAD(glUniformBlockBinding, PFNGLUNIFORMBLOCKBINDINGPROC);
        TGFX_LOAD_WEBGL2_GLAD(glVertexAttribDivisor, PFNGLVERTEXATTRIBDIVISORPROC);
#undef TGFX_LOAD_WEBGL2_GLAD
#define TGFX_REQUIRE_WEBGL2_PROC(proc)                                                                                 \
    do {                                                                                                               \
        if (!(proc)) {                                                                                                 \
            tc_log_error("WebGL2 required entry point is unavailable: %s", #proc);                                    \
            throw std::runtime_error("WebGL2 required entry point is unavailable: " #proc);                           \
        }                                                                                                              \
    } while (false)
        TGFX_REQUIRE_WEBGL2_PROC(glGetString);
        TGFX_REQUIRE_WEBGL2_PROC(glGetIntegerv);
        TGFX_REQUIRE_WEBGL2_PROC(glGenBuffers);
        TGFX_REQUIRE_WEBGL2_PROC(glBindBuffer);
        TGFX_REQUIRE_WEBGL2_PROC(glBufferData);
        TGFX_REQUIRE_WEBGL2_PROC(glBufferSubData);
        TGFX_REQUIRE_WEBGL2_PROC(glBindBufferBase);
        TGFX_REQUIRE_WEBGL2_PROC(glBindBufferRange);
        TGFX_REQUIRE_WEBGL2_PROC(glGenTextures);
        TGFX_REQUIRE_WEBGL2_PROC(glBindTexture);
        TGFX_REQUIRE_WEBGL2_PROC(glTexImage2D);
        TGFX_REQUIRE_WEBGL2_PROC(glTexSubImage2D);
        TGFX_REQUIRE_WEBGL2_PROC(glGenSamplers);
        TGFX_REQUIRE_WEBGL2_PROC(glBindSampler);
        TGFX_REQUIRE_WEBGL2_PROC(glSamplerParameteri);
        TGFX_REQUIRE_WEBGL2_PROC(glCreateShader);
        TGFX_REQUIRE_WEBGL2_PROC(glShaderSource);
        TGFX_REQUIRE_WEBGL2_PROC(glCompileShader);
        TGFX_REQUIRE_WEBGL2_PROC(glGetShaderiv);
        TGFX_REQUIRE_WEBGL2_PROC(glCreateProgram);
        TGFX_REQUIRE_WEBGL2_PROC(glAttachShader);
        TGFX_REQUIRE_WEBGL2_PROC(glLinkProgram);
        TGFX_REQUIRE_WEBGL2_PROC(glGetProgramiv);
        TGFX_REQUIRE_WEBGL2_PROC(glUseProgram);
        TGFX_REQUIRE_WEBGL2_PROC(glGetUniformBlockIndex);
        TGFX_REQUIRE_WEBGL2_PROC(glUniformBlockBinding);
        TGFX_REQUIRE_WEBGL2_PROC(glGetUniformLocation);
        TGFX_REQUIRE_WEBGL2_PROC(glUniform1i);
        TGFX_REQUIRE_WEBGL2_PROC(glGenVertexArrays);
        TGFX_REQUIRE_WEBGL2_PROC(glBindVertexArray);
        TGFX_REQUIRE_WEBGL2_PROC(glEnableVertexAttribArray);
        TGFX_REQUIRE_WEBGL2_PROC(glVertexAttribPointer);
        TGFX_REQUIRE_WEBGL2_PROC(glGenFramebuffers);
        TGFX_REQUIRE_WEBGL2_PROC(glBindFramebuffer);
        TGFX_REQUIRE_WEBGL2_PROC(glFramebufferTexture2D);
        TGFX_REQUIRE_WEBGL2_PROC(glDrawBuffers);
        TGFX_REQUIRE_WEBGL2_PROC(glReadBuffer);
        TGFX_REQUIRE_WEBGL2_PROC(glClearBufferfv);
        TGFX_REQUIRE_WEBGL2_PROC(glDrawArrays);
        TGFX_REQUIRE_WEBGL2_PROC(glDrawElements);
#undef TGFX_REQUIRE_WEBGL2_PROC
#endif

        // Align OpenGL's NDC + window-coord convention with Vulkan:
        //   GL_UPPER_LEFT   — window-coord origin at top-left (like Vulkan),
        //                     so glScissor / glViewport y=0 is the top row
        //                     and no per-call Y-flip is needed.
        //   GL_ZERO_TO_ONE  — clip-space Z ∈ [0, 1] (Vulkan default).
        // All our projection matrices (termin-base/geom/mat44.hpp) target
        // this convention; shaders write clip-space Y pointing down already.
        // Requires GL 4.5 or ARB_clip_control (ubiquitous on desktop GL).
        query_capabilities();
        enforce_clip_space_contract();
        ensure_ring_ubo();

        tc_texture_registry_add_destroy_hook(&opengl_invalidate_tc_texture_trampoline, this);
        tc_mesh_registry_add_destroy_hook(&opengl_invalidate_tc_mesh_trampoline, this);
        tc_shader_registry_add_destroy_hook(&opengl_invalidate_tc_shader_trampoline, this);
    }

    void OpenGLRenderDevice::enforce_clip_space_contract() {
        std::string error;
        if (!gl_operations_ || !gl_operations_->apply_clip_space_contract(error))
            throw std::runtime_error("OpenGL backend could not establish its clip-space contract: " + error);
    }

    OpenGLRenderDevice::~OpenGLRenderDevice() {
        tc_shader_registry_remove_destroy_hook(&opengl_invalidate_tc_shader_trampoline, this);
        tc_mesh_registry_remove_destroy_hook(&opengl_invalidate_tc_mesh_trampoline, this);
        tc_texture_registry_remove_destroy_hook(&opengl_invalidate_tc_texture_trampoline, this);

        destroy_pixel_readback_resources();

        for (auto& pair : tc_shader_cache_) {
            auto& entry = pair.second;
            if (entry.vs)
                destroy(entry.vs);
            if (entry.fs)
                destroy(entry.fs);
        }
        tc_shader_cache_.clear();

        for (auto& pair : tc_mesh_cache_) {
            auto& entry = pair.second;
            if (entry.vbo)
                destroy(entry.vbo);
            if (entry.ebo)
                destroy(entry.ebo);
        }
        tc_mesh_cache_.clear();

        for (auto& pair : tc_texture_cache_) {
            auto& entry = pair.second;
            if (entry.handle)
                destroy(entry.handle);
        }
        tc_texture_cache_.clear();

        // Clean up cached FBOs
        for (auto& [key, fbo] : fbo_cache_) {
            if (fbo)
                glDeleteFramebuffers(1, &fbo);
        }
        fbo_cache_.clear();

        // Clean up push constants ring buffer
        if (push_ring_buf_) {
            glDeleteBuffers(1, &push_ring_buf_);
            push_ring_buf_ = 0;
        }

        // Clean up transient vertex ring
        if (transient_vb_gl_) {
            glDeleteBuffers(1, &transient_vb_gl_);
            transient_vb_gl_ = 0;
            // Release the BufferHandle slot in our HandlePool too.
            if (transient_vb_handle_) {
                // Don't route through destroy(BufferHandle) — that would
                // glDeleteBuffers the id we just freed above. Just drop the
                // slot.
                buffers_.remove(transient_vb_handle_.id);
                transient_vb_handle_ = {};
            }
        }

        // Clean up dynamic UBO ring
        if (ring_ubo_gl_) {
            glDeleteBuffers(1, &ring_ubo_gl_);
            ring_ubo_gl_ = 0;
            if (ring_ubo_handle_) {
                buffers_.remove(ring_ubo_handle_.id);
                ring_ubo_handle_ = {};
            }
        }

        // Clean up all remaining GL resources
        for (auto& [id, buf] : buffers_) {
            if (buf.gl_id)
                glDeleteBuffers(1, &buf.gl_id);
        }
        for (auto& [id, tex] : textures_) {
            if (tex.gl_id)
                glDeleteTextures(1, &tex.gl_id);
        }
        for (auto& [id, s] : samplers_) {
            if (s.gl_id)
                glDeleteSamplers(1, &s.gl_id);
        }
        for (auto& [id, sh] : shaders_) {
            if (sh.gl_shader)
                glDeleteShader(sh.gl_shader);
        }
        for (auto& [key, shared] : program_cache_) {
            if (shared.program)
                glDeleteProgram(shared.program);
        }
    }

    void OpenGLRenderDevice::query_capabilities() {
        caps_.backend = BackendType::OpenGL;
        caps_.texture_origin_top_left = true;

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);

        GLint val = 0;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &val);
        const uint32_t max_color_attachments = static_cast<uint32_t>(val);

        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &val);
        const uint32_t max_texture_dimension_2d = static_cast<uint32_t>(val);

        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &val);
        const uint32_t max_texture_units = static_cast<uint32_t>(val);

        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &val);
        const uint32_t max_fragment_texture_units = static_cast<uint32_t>(val);

        GlRuntimeInfo runtime;
#if defined(__EMSCRIPTEN__)
        runtime.api = GlApi::WebGL;
        runtime.major = 2;
        runtime.minor = 0;
#else
        runtime.api = GlApi::Desktop;
        runtime.major = static_cast<uint32_t>(major);
        runtime.minor = static_cast<uint32_t>(minor);
#endif
        runtime.has_clip_control = GlPlatformOperations::runtime_has_clip_control(
            static_cast<uint32_t>(major), static_cast<uint32_t>(minor));
        runtime.has_polygon_mode = glPolygonMode != nullptr;
        runtime.has_base_vertex_draws = glDrawElementsBaseVertex != nullptr &&
                                        glDrawElementsInstancedBaseVertex != nullptr;
        runtime.has_multisample_textures = glTexImage2DMultisample != nullptr;
        runtime.has_timestamp_queries = glQueryCounter != nullptr && glGetQueryObjectui64v != nullptr;
        // Preserve the current public capability contract until the shared GL
        // command path actually implements compute dispatch.
        runtime.has_compute = false;
        runtime.has_geometry_shaders = major > 3 || (major == 3 && minor >= 2);
        runtime.max_color_attachments = max_color_attachments;
        runtime.max_texture_dimension_2d = max_texture_dimension_2d;
        runtime.max_texture_units = max_texture_units;
        runtime.max_fragment_texture_units = max_fragment_texture_units;

        std::string feature_error;
        if (!derive_gl_feature_set(requested_tier_, runtime, gl_features_, feature_error)) {
            tc_log_error("OpenGL feature-tier validation failed: %s", feature_error.c_str());
            throw std::runtime_error(feature_error);
        }
        gl_coordinates_ = gl_coordinate_contract(gl_features_.tier);
        gl_operations_ = std::make_unique<GlPlatformOperations>(gl_features_);

        const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        adapter_info_.backend = BackendType::OpenGL;
        adapter_info_.adapter_name = renderer ? renderer : "unknown OpenGL renderer";
        adapter_info_.driver_name = std::string(vendor ? vendor : "unknown vendor") + " / " +
                                    (version ? version : "unknown version");
        std::string renderer_lower = adapter_info_.adapter_name;
        std::transform(renderer_lower.begin(), renderer_lower.end(), renderer_lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        adapter_info_.hardware_class =
            renderer_lower.find("llvmpipe") != std::string::npos ||
                    renderer_lower.find("softpipe") != std::string::npos ||
                    renderer_lower.find("swiftshader") != std::string::npos ||
                    renderer_lower.find("software") != std::string::npos
                ? AdapterClass::Cpu
                : AdapterClass::Unknown;
        tc_log_info("OpenGL device initialized: tier=%s target=%s api=%d.%d renderer='%s' driver='%s' "
                    "fragment_textures=%u shadow_maps=%u",
                    gl_feature_tier_name(gl_features_.tier),
                    shader_artifact_target_name(gl_features_.shader_target),
                    major,
                    minor,
                    adapter_info_.adapter_name.c_str(),
                    adapter_info_.driver_name.c_str(),
                    gl_features_.max_fragment_texture_units,
                    gl_features_.max_shadow_maps);

        caps_.shader_artifact_target = gl_features_.shader_target;
        caps_.max_color_attachments = gl_features_.max_color_attachments;
        caps_.max_texture_dimension_2d = gl_features_.max_texture_dimension_2d;
        caps_.max_texture_units = gl_features_.max_texture_units;
        caps_.max_fragment_texture_units = gl_features_.max_fragment_texture_units;
        caps_.max_shadow_maps = gl_features_.max_shadow_maps;

        caps_.supports_compute = gl_features_.supports_compute;
        caps_.supports_geometry_shaders = gl_features_.supports_geometry_shaders;
        caps_.supports_timestamp_queries = gl_features_.supports_timestamp_queries;
        caps_.supports_multisample_resolve = true;
        caps_.supports_dynamic_uniform_offsets = false;
        caps_.supports_storage_textures = false;
    }

    BackendCapabilities OpenGLRenderDevice::capabilities() const {
        return caps_;
    }

    void OpenGLRenderDevice::wait_idle() {
        glFinish();
    }

    namespace {

        TextureUsage tc_usage_to_tgfx(uint32_t usage) {
            uint32_t out = 0;
            if (usage & TC_TEXTURE_USAGE_SAMPLED)
                out |= static_cast<uint32_t>(TextureUsage::Sampled);
            if (usage & TC_TEXTURE_USAGE_COLOR_ATTACHMENT)
                out |= static_cast<uint32_t>(TextureUsage::ColorAttachment);
            if (usage & TC_TEXTURE_USAGE_DEPTH_ATTACHMENT)
                out |= static_cast<uint32_t>(TextureUsage::DepthStencilAttachment);
            if (usage & TC_TEXTURE_USAGE_COPY_SRC)
                out |= static_cast<uint32_t>(TextureUsage::CopySrc);
            if (usage & TC_TEXTURE_USAGE_COPY_DST)
                out |= static_cast<uint32_t>(TextureUsage::CopyDst);
            return static_cast<TextureUsage>(out);
        }

        bool load_opengl_shader_artifact_source(const termin::ShaderArtifactResolver& resolver,
                                                tc_shader* shader,
                                                ShaderArtifactTarget target,
                                                ShaderStage stage,
                                                std::string& out_source) {
            std::vector<uint8_t> bytes;
            if (!termin::tgfx2_load_or_compile_shader_artifact_for_target(resolver, shader, target, stage, bytes)) {
                return false;
            }
            out_source.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }

    } // anonymous namespace

    TextureHandle OpenGLRenderDevice::ensure_tc_texture(tc_texture* tex) {
        if (!tex)
            return {};

        if (!tex->header.is_loaded) {
            tc_texture_ensure_loaded_ptr(tex);
        }

        const bool gpu_first = (tex->storage_kind == TC_TEXTURE_STORAGE_GPU_FIRST);
        if (tex->width == 0 || tex->height == 0) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_texture: tc_texture '%s' has zero size",
                         tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }
        if (!gpu_first && !tex->data) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_texture: tc_texture '%s' has no CPU pixels",
                         tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }

        const uint32_t pool_index = tex->header.pool_index;
        const uint32_t version = tex->header.version;
        auto it = tc_texture_cache_.find(pool_index);
        if (it != tc_texture_cache_.end() && it->second.version == version) {
            return it->second.handle;
        }
        if (it != tc_texture_cache_.end()) {
            if (it->second.handle)
                destroy(it->second.handle);
            tc_texture_cache_.erase(it);
        }

        TextureDesc desc;
        desc.width = tex->width;
        desc.height = tex->height;
        desc.sample_count = 1;
        desc.format = pixel_format_for_tc_texture(static_cast<tc_texture_format>(tex->format),
                                                  static_cast<tc_texture_encoding>(tex->encoding));
        if (desc.format == PixelFormat::Undefined) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_texture: texture '%s' has unsupported format/encoding %u/%u",
                         tex->header.name ? tex->header.name : tex->header.uuid,
                         static_cast<unsigned>(tex->format),
                         static_cast<unsigned>(tex->encoding));
            return {};
        }
        desc.mip_levels = 1;

        if (gpu_first) {
            if (tex->mipmap) {
                tc_log_error("OpenGLRenderDevice::ensure_tc_texture: GPU-first texture '%s' requests mipmaps without a "
                             "source chain",
                             tex->header.name ? tex->header.name : tex->header.uuid);
                return {};
            }
            desc.usage = tc_usage_to_tgfx(tex->usage) | TextureUsage::CopyDst;
            if (static_cast<uint32_t>(desc.usage) == static_cast<uint32_t>(TextureUsage::CopyDst)) {
                desc.usage = desc.usage | TextureUsage::Sampled;
            }
            TextureHandle handle = create_texture(desc);
            if (!handle) {
                tc_log_error("OpenGLRenderDevice::ensure_tc_texture: create GPU-first texture failed for '%s'",
                             tex->header.name ? tex->header.name : tex->header.uuid);
                return {};
            }
            if (tex->compare_mode) {
                if (auto* gl_tex = get_texture(handle)) {
                    glBindTexture(gl_tex->target, gl_tex->gl_id);
                    glTexParameteri(gl_tex->target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                    glTexParameteri(gl_tex->target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                    glBindTexture(gl_tex->target, 0);
                }
            }
            CachedTcTextureEntry entry;
            entry.handle = handle;
            entry.version = version;
            tc_texture_cache_.emplace(pool_index, entry);
            return handle;
        }

        desc.usage = TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst;

        TcTextureUpload upload;
        if (!prepare_tc_texture_upload(tex, upload)) {
            return {};
        }
        desc.format = upload.format;
        desc.mip_levels = static_cast<uint32_t>(upload.levels.size());

        TextureHandle handle = create_texture(desc);
        if (!handle) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_texture: create_texture failed for '%s'",
                         tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }

        for (uint32_t mip = 0; mip < upload.levels.size(); ++mip) {
            const auto& pixels = upload.levels[mip];
            upload_texture(handle, std::span<const uint8_t>(pixels.data(), pixels.size()), mip);
        }

        if (auto* gl_tex = get_texture(handle)) {
            glBindTexture(gl_tex->target, gl_tex->gl_id);
            if (desc.mip_levels > 1) {
                glTexParameteri(gl_tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            }
            glTexParameteri(gl_tex->target, GL_TEXTURE_WRAP_S, tex->clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(gl_tex->target, GL_TEXTURE_WRAP_T, tex->clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            if (tex->compare_mode) {
                glTexParameteri(gl_tex->target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                glTexParameteri(gl_tex->target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            }
            glBindTexture(gl_tex->target, 0);
        }

        CachedTcTextureEntry entry;
        entry.handle = handle;
        entry.version = version;
        tc_texture_cache_.emplace(pool_index, entry);
        return handle;
    }

    void OpenGLRenderDevice::invalidate_tc_texture_cache(uint32_t pool_index) {
        auto it = tc_texture_cache_.find(pool_index);
        if (it == tc_texture_cache_.end())
            return;
        if (it->second.handle)
            destroy(it->second.handle);
        tc_texture_cache_.erase(it);
    }

    std::pair<BufferHandle, BufferHandle> OpenGLRenderDevice::ensure_tc_mesh(tc_mesh* mesh) {
        if (!mesh)
            return {};

        if (!mesh->vertices || mesh->vertex_count == 0 || !mesh->indices || mesh->index_count == 0 ||
            mesh->layout.stride == 0) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_mesh: tc_mesh '%s' has no CPU mesh data",
                         mesh->header.name ? mesh->header.name : mesh->header.uuid);
            return {};
        }

        const uint32_t pool_index = mesh->header.pool_index;
        const uint32_t version = mesh->header.version;
        auto it = tc_mesh_cache_.find(pool_index);
        if (it != tc_mesh_cache_.end() && it->second.version == version) {
            return {it->second.vbo, it->second.ebo};
        }
        if (it != tc_mesh_cache_.end()) {
            if (it->second.vbo)
                destroy(it->second.vbo);
            if (it->second.ebo)
                destroy(it->second.ebo);
            tc_mesh_cache_.erase(it);
        }

        const size_t vb_size = static_cast<size_t>(mesh->vertex_count) * static_cast<size_t>(mesh->layout.stride);
        const size_t ib_size = static_cast<size_t>(mesh->index_count) * sizeof(uint32_t);

        BufferDesc vb_desc;
        vb_desc.size = vb_size;
        vb_desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        BufferHandle vbo = create_buffer(vb_desc);
        if (!vbo) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_mesh: failed to allocate VBO for '%s'",
                         mesh->header.name ? mesh->header.name : mesh->header.uuid);
            return {};
        }
        upload_buffer(vbo, std::span<const uint8_t>(static_cast<const uint8_t*>(mesh->vertices), vb_size));

        BufferDesc ib_desc;
        ib_desc.size = ib_size;
        ib_desc.usage = BufferUsage::Index | BufferUsage::CopyDst;
        BufferHandle ebo = create_buffer(ib_desc);
        if (!ebo) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_mesh: failed to allocate EBO for '%s'",
                         mesh->header.name ? mesh->header.name : mesh->header.uuid);
            destroy(vbo);
            return {};
        }
        upload_buffer(ebo, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(mesh->indices), ib_size));

        CachedTcMeshEntry entry;
        entry.vbo = vbo;
        entry.ebo = ebo;
        entry.version = version;
        tc_mesh_cache_.emplace(pool_index, entry);
        return {vbo, ebo};
    }

    void OpenGLRenderDevice::invalidate_tc_mesh_cache(uint32_t pool_index) {
        auto it = tc_mesh_cache_.find(pool_index);
        if (it == tc_mesh_cache_.end())
            return;
        if (it->second.vbo)
            destroy(it->second.vbo);
        if (it->second.ebo)
            destroy(it->second.ebo);
        tc_mesh_cache_.erase(it);
    }

    bool OpenGLRenderDevice::ensure_tc_shader(tc_shader* shader, ShaderHandle* out_vs, ShaderHandle* out_fs) {
        if (!shader) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_shader: shader is NULL");
            return false;
        }
        if (!out_fs) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_shader: out_fs is NULL");
            return false;
        }
        if (!shader->fragment_source) {
            tc_log_error("OpenGLRenderDevice::ensure_tc_shader: missing fragment_source for '%s'",
                         shader->name ? shader->name : shader->uuid);
            return false;
        }

        const bool has_vs = shader->vertex_source && shader->vertex_source[0] != '\0';
        const bool artifacts_required = tc_shader_requires_artifacts(shader);
        const auto shader_language = static_cast<tc_shader_language>(shader->language);
        const uint32_t pool_index = shader->pool_index;
        const uint32_t version = shader->version;
        const auto& resolver = shader_artifact_resolver();
        const uint64_t resolver_revision = resolver.revision();
        const bool resource_layout_ready = tc_shader_has_resource_layout(shader) ||
                                           (!artifacts_required && shader_language == TC_SHADER_LANGUAGE_GLSL);
        auto it = tc_shader_cache_.find(pool_index);
        if (it != tc_shader_cache_.end() && it->second.version == version &&
            it->second.resolver_revision == resolver_revision && it->second.has_vs == has_vs && it->second.fs &&
            (!has_vs || it->second.vs) && resource_layout_ready) {
            if (out_vs)
                *out_vs = it->second.vs;
            *out_fs = it->second.fs;
            return true;
        }

        if (it != tc_shader_cache_.end()) {
            if (it->second.vs)
                destroy(it->second.vs);
            if (it->second.fs)
                destroy(it->second.fs);
            tc_shader_cache_.erase(it);
        }

        ShaderHandle vs;
        if (has_vs) {
            ShaderDesc vs_desc;
            vs_desc.stage = ShaderStage::Vertex;
            vs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":vertex";
            if (!load_opengl_shader_artifact_source(
                    resolver, shader, shader_artifact_target(), vs_desc.stage, vs_desc.source)) {
                if (artifacts_required || shader_language != TC_SHADER_LANGUAGE_GLSL) {
                    tc_log_error("OpenGLRenderDevice::ensure_tc_shader: %s vertex artifact missing or dev compile "
                                 "failed for '%s' language=%u",
                                 artifacts_required ? "required" : "non-GLSL",
                                 shader->name ? shader->name : shader->uuid,
                                 static_cast<unsigned>(shader->language));
                    return false;
                }
                vs_desc.source = shader->vertex_source;
            }
            vs = create_shader(vs_desc);
            if (!vs) {
                tc_log_error("OpenGLRenderDevice::ensure_tc_shader: VS compile failed for '%s'",
                             shader->name ? shader->name : shader->uuid);
                return false;
            }
        }

        ShaderDesc fs_desc;
        fs_desc.stage = ShaderStage::Fragment;
        fs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":fragment";
        if (!load_opengl_shader_artifact_source(
                resolver, shader, shader_artifact_target(), fs_desc.stage, fs_desc.source)) {
            if (artifacts_required || shader_language != TC_SHADER_LANGUAGE_GLSL) {
                if (vs)
                    destroy(vs);
                tc_log_error("OpenGLRenderDevice::ensure_tc_shader: %s fragment artifact missing or dev compile failed "
                             "for '%s' language=%u",
                             artifacts_required ? "required" : "non-GLSL",
                             shader->name ? shader->name : shader->uuid,
                             static_cast<unsigned>(shader->language));
                return false;
            }
            fs_desc.source = shader->fragment_source;
        }
        ShaderHandle fs = create_shader(fs_desc);
        if (!fs) {
            if (vs)
                destroy(vs);
            tc_log_error("OpenGLRenderDevice::ensure_tc_shader: FS compile failed for '%s'",
                         shader->name ? shader->name : shader->uuid);
            return false;
        }

        CachedTcShaderEntry entry;
        entry.vs = vs;
        entry.fs = fs;
        entry.version = version;
        entry.resolver_revision = resolver_revision;
        entry.has_vs = has_vs;
        tc_shader_cache_.emplace(pool_index, entry);

        if (out_vs)
            *out_vs = vs;
        *out_fs = fs;
        return true;
    }

    void OpenGLRenderDevice::invalidate_tc_shader_cache(uint32_t pool_index) {
        auto it = tc_shader_cache_.find(pool_index);
        if (it == tc_shader_cache_.end())
            return;
        if (it->second.vs)
            destroy(it->second.vs);
        if (it->second.fs)
            destroy(it->second.fs);
        tc_shader_cache_.erase(it);
    }

    // --- Buffer ---

    BufferHandle OpenGLRenderDevice::create_buffer(const BufferDesc& desc) {
        GLBuffer buf;
        buf.desc = desc;
        buf.target = gl::to_gl_buffer_target(desc.usage);

        glGenBuffers(1, &buf.gl_id);
        glBindBuffer(buf.target, buf.gl_id);
        glBufferData(buf.target, static_cast<GLsizeiptr>(desc.size), nullptr, gl::to_gl_buffer_usage(desc.cpu_visible));
        glBindBuffer(buf.target, 0);

        return {buffers_.add(std::move(buf))};
    }

    // --- Texture ---

    TextureHandle OpenGLRenderDevice::create_texture(const TextureDesc& desc) {
        if (desc.array_layers != 1) {
            tc_log(TC_LOG_ERROR, "OpenGLRenderDevice::create_texture: layered textures are not supported");
            return {};
        }
        if (has_flag(desc.usage, TextureUsage::Storage)) {
            tc::Log::error("OpenGLRenderDevice::create_texture: TextureUsage::Storage is not "
                           "supported by the GL 3.3 backend");
            return {};
        }

        GLTexture tex;
        tex.desc = desc;

        auto fmt = gl::to_gl_format(desc.format);

        if (desc.sample_count > 1) {
            tex.target = GL_TEXTURE_2D_MULTISAMPLE;
            glGenTextures(1, &tex.gl_id);
            glBindTexture(tex.target, tex.gl_id);
            if (!gl_operations_->allocate_multisample_texture_2d(
                    tex.target, desc.sample_count, fmt.internal_format, desc.width, desc.height, true)) {
                glBindTexture(tex.target, 0);
                glDeleteTextures(1, &tex.gl_id);
                return {};
            }
        } else {
            tex.target = GL_TEXTURE_2D;
            glGenTextures(1, &tex.gl_id);
            glBindTexture(tex.target, tex.gl_id);
            // Allocate storage for all mip levels
            for (uint32_t mip = 0; mip < desc.mip_levels; ++mip) {
                uint32_t w = std::max(1u, desc.width >> mip);
                uint32_t h = std::max(1u, desc.height >> mip);
                glTexImage2D(tex.target, mip, fmt.internal_format, w, h, 0, fmt.format, fmt.type, nullptr);
            }
            // Mandatory defaults: without these GL treats the texture as
            // incomplete (default MIN filter is NEAREST_MIPMAP_LINEAR,
            // which requires a full mip chain). Any sampled texture binding
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
        glSamplerParameteri(samp.gl_id, GL_TEXTURE_MIN_FILTER, gl::to_gl_min_filter(desc.min_filter, desc.mip_filter));
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

    // GLSL overlay injected after #version / extension lines on OpenGL
    // shaders. Pairs with the Y-flip of upload_texture payloads so that
    // sampling reads the "Vulkan row 0 = visual top" convention uniformly
    // on both backends. See docs/coord_system.md §4.
    //
    // Rules:
    //   - Only sampler2D / sampler2DShadow get the V-flip. 3D / Cube / array
    //     shadow variants pass the coord through unchanged.
    //   - textureLod, textureGrad, texelFetch keep the same flip behaviour.
    //   - Other overloads (textureGather, textureProj, etc.) are *not*
    //     covered yet — add them here if a shader needs them.
    //   - The helpers are declared before the `#define` so that their bodies
    //     can still call the original builtin `texture()` / `texelFetch()`.
    //     Macros only expand in source that follows the `#define` lines,
    //     so the helpers' internal use of `texture` / `texelFetch` resolves
    //     to the builtin, not to itself.
    static constexpr const char* kGLSamplingFlipOverlay =
        "// tgfx2-GL-sampling-flip\n"
        "vec4  _tgfx_gl_tex(highp sampler2D s, vec2 uv)             { return texture(s, vec2(uv.x, 1.0 - uv.y)); }\n"
        "vec4  _tgfx_gl_tex(highp sampler2D s, vec2 uv, float lod)  { return textureLod(s, vec2(uv.x, 1.0 - uv.y), lod); }\n"
        "vec4  _tgfx_gl_tex(highp sampler3D s, vec3 uvw)            { return texture(s, uvw); }\n"
        "vec4  _tgfx_gl_tex(highp samplerCube s, vec3 dir)          { return texture(s, dir); }\n"
        "float _tgfx_gl_tex(highp sampler2DShadow s, vec3 uvz)      { return texture(s, vec3(uvz.x, 1.0 - uvz.y, uvz.z)); }\n"
        "vec4  _tgfx_gl_texel(highp sampler2D s, ivec2 p, int lod) {\n"
        "    ivec2 sz = textureSize(s, lod);\n"
        "    return texelFetch(s, ivec2(p.x, sz.y - 1 - p.y), lod);\n"
        "}\n"
        "#define texture _tgfx_gl_tex\n"
        "#define texelFetch _tgfx_gl_texel\n";

    static constexpr const char* kWebGL2PrecisionOverlay =
        "// tgfx2-WebGL2-default-precision\n"
        "precision highp float;\n"
        "precision highp int;\n";

    // Find the end of the #version directive (and subsequent #extension
    // lines — they must precede any non-preprocessor code in GLSL) and
    // insert `overlay` there. If no #version is present, insert at the
    // very start.
    static std::string inject_after_version(const std::string& source, const char* overlay) {
        size_t ver_pos = source.find("#version");
        if (ver_pos == std::string::npos) {
            return std::string(overlay) + source;
        }
        // Find the end of the line that contains #version.
        size_t eol = source.find('\n', ver_pos);
        if (eol == std::string::npos) {
            return source + "\n" + overlay;
        }
        // Consume any subsequent #extension lines so we don't split the
        // preprocessor block.
        size_t insert_pos = eol + 1;
        while (insert_pos < source.size()) {
            // Skip whitespace/newlines between directives.
            size_t line_start = insert_pos;
            while (line_start < source.size() && (source[line_start] == ' ' || source[line_start] == '\t' ||
                                                  source[line_start] == '\r' || source[line_start] == '\n')) {
                ++line_start;
            }
            if (source.compare(line_start, 10, "#extension") != 0)
                break;
            size_t next_eol = source.find('\n', line_start);
            if (next_eol == std::string::npos) {
                insert_pos = source.size();
                break;
            }
            insert_pos = next_eol + 1;
        }
        std::string out;
        out.reserve(source.size() + std::strlen(overlay));
        out.append(source, 0, insert_pos);
        out.append(overlay);
        out.append(source, insert_pos, std::string::npos);
        return out;
    }

    ShaderHandle OpenGLRenderDevice::create_shader(const ShaderDesc& desc) {
        if (desc.source.empty()) {
            return {0};
        }

        GLShaderModule mod;
        mod.stage = desc.stage;
        mod.debug_name = desc.debug_name.empty() ? "<unnamed>" : desc.debug_name;

        if (desc.stage == ShaderStage::Compute && !caps_.supports_compute) {
            tc_log_error("OpenGL shader rejected: name='%s' stage=compute target=%s reason=unsupported stage",
                         mod.debug_name.c_str(),
                         shader_artifact_target_name(gl_features_.shader_target));
            throw std::runtime_error("OpenGL target " +
                                     std::string(shader_artifact_target_name(gl_features_.shader_target)) +
                                     " does not support compute shader '" + mod.debug_name + "'");
        }
        if (desc.stage == ShaderStage::Geometry && !caps_.supports_geometry_shaders) {
            tc_log_error("OpenGL shader rejected: name='%s' stage=geometry target=%s reason=unsupported stage",
                         mod.debug_name.c_str(),
                         shader_artifact_target_name(gl_features_.shader_target));
            throw std::runtime_error("OpenGL target " +
                                     std::string(shader_artifact_target_name(gl_features_.shader_target)) +
                                     " does not support geometry shader '" + mod.debug_name + "'");
        }

        GLenum gl_stage = gl::to_gl_shader_stage(desc.stage);
        mod.gl_shader = glCreateShader(gl_stage);

        std::string resolved = desc.source;
        if (gl_features_.tier == GlFeatureTier::WebGL2) {
            const std::string overlay = std::string(kWebGL2PrecisionOverlay) + kGLSamplingFlipOverlay;
            resolved = inject_after_version(resolved, overlay.c_str());
        } else {
            resolved = inject_after_version(resolved, kGLSamplingFlipOverlay);
        }
        // On OpenGL, wrap sampling builtins so v=0 = top of content
        // regardless of whether the texture was uploaded from CPU (flipped
        // in upload_texture) or rendered into (bottom-up FBO memory).
        const char* src = resolved.c_str();

        glShaderSource(mod.gl_shader, 1, &src, nullptr);
        glCompileShader(mod.gl_shader);

        GLint status;
        glGetShaderiv(mod.gl_shader, GL_COMPILE_STATUS, &status);
        if (!status) {
            GLint log_size = 0;
            glGetShaderiv(mod.gl_shader, GL_INFO_LOG_LENGTH, &log_size);
            std::string log(static_cast<size_t>(std::max(log_size, 1)), '\0');
            glGetShaderInfoLog(mod.gl_shader, log_size, nullptr, log.data());
            glDeleteShader(mod.gl_shader);
            tc_log_error("OpenGL shader compile failed: name='%s' stage=%s target=%s: %s",
                         mod.debug_name.c_str(),
                         gl_shader_stage_name(mod.stage),
                         shader_artifact_target_name(gl_features_.shader_target),
                         log.c_str());
            throw std::runtime_error("OpenGL shader compile failed for '" + mod.debug_name + "' stage=" +
                                     gl_shader_stage_name(mod.stage) + " target=" +
                                     shader_artifact_target_name(gl_features_.shader_target) + ": " + log);
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
            GLint log_size = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_size);
            std::string log(static_cast<size_t>(std::max(log_size, 1)), '\0');
            glGetProgramInfoLog(program, log_size, nullptr, log.data());
            glDeleteProgram(program);
            const char* gs_name = "<none>";
            if (desc.geometry_shader) {
                if (const auto* gs = get_shader(desc.geometry_shader))
                    gs_name = gs->debug_name.c_str();
            }
            tc_log_error("OpenGL program link failed: target=%s vertex='%s' fragment='%s' geometry='%s': %s",
                         shader_artifact_target_name(gl_features_.shader_target),
                         vs->debug_name.c_str(),
                         fs->debug_name.c_str(),
                         gs_name,
                         log.c_str());
            throw std::runtime_error("OpenGL program link failed target=" +
                                     std::string(shader_artifact_target_name(gl_features_.shader_target)) +
                                     " vertex='" + vs->debug_name + "' fragment='" + fs->debug_name +
                                     "' geometry='" + gs_name + "': " + log);
        }

        program_cache_[key] = GLSharedProgram{program, 1};
        program_to_key_[program] = key;
        return program;
    }

    void OpenGLRenderDevice::release_program(GLuint program) {
        if (program == 0)
            return;
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

    ResourceSetHandle OpenGLRenderDevice::create_bound_resource_set(const BoundResourceSetDesc& desc) {
        GLResourceSet rs;
        rs.bound_resources.assign(desc);
        return {resource_sets_.add(std::move(rs))};
    }

    uintptr_t OpenGLRenderDevice::pipeline_resource_layout_token(PipelineHandle pipeline) const {
        if (!pipelines_.get_const(pipeline.id)) {
            return 0;
        }

        // OpenGL has no descriptor set layout object, but RenderContext2 still
        // needs a non-zero, stable token to decide when a pipeline has a distinct
        // resource binding layout and when pending bind-by-name data may be
        // flushed into a ResourceSet.
        return static_cast<uintptr_t>(pipeline.id);
    }

    uintptr_t OpenGLRenderDevice::pipeline_descriptor_set_layout(PipelineHandle pipeline) const {
        return pipeline_resource_layout_token(pipeline);
    }

    // --- Destroy ---

    void OpenGLRenderDevice::destroy(BufferHandle handle) {
        if (auto* buf = buffers_.get(handle.id)) {
            if (buf->gl_id && !buf->external)
                glDeleteBuffers(1, &buf->gl_id);
            buffers_.remove(handle.id);
        }
    }

    void OpenGLRenderDevice::destroy(TextureHandle handle) {
        if (auto* tex = textures_.get(handle.id)) {
            const GLuint deleted_gl_id = tex->gl_id;

            if (tex->gl_id && !tex->external)
                glDeleteTextures(1, &tex->gl_id);
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
                for (auto it = fbo_cache_.begin(); it != fbo_cache_.end();) {
                    bool match = false;
                    for (const auto& [attach_point, tex_id] : it->first) {
                        if (tex_id == deleted_gl_id) {
                            match = true;
                            break;
                        }
                    }
                    if (match) {
                        if (it->second != 0)
                            glDeleteFramebuffers(1, &it->second);
                        it = fbo_cache_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }

    TextureHandle OpenGLRenderDevice::register_external_texture(uintptr_t native_handle, const TextureDesc& desc) {
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

    BufferHandle OpenGLRenderDevice::register_external_buffer(uintptr_t native_handle, const BufferDesc& desc) {
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
            if (s->gl_id)
                glDeleteSamplers(1, &s->gl_id);
            samplers_.remove(handle.id);
        }
    }

    void OpenGLRenderDevice::destroy(ShaderHandle handle) {
        if (auto* sh = shaders_.get(handle.id)) {
            if (sh->gl_shader)
                glDeleteShader(sh->gl_shader);
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
        if (!buf)
            return;

        glBindBuffer(buf->target, buf->gl_id);
        glBufferSubData(buf->target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size()), data.data());
        glBindBuffer(buf->target, 0);
    }

    // Fill `dst` with the byte-reversed rows of `src`. Caller supplies
    // `row_bytes` (bytes per row) and `rows` (number of rows). dst and src
    // must not overlap. Used by the OpenGL backend to Y-flip upload payloads
    // so that sampling (also Y-flipped in shaders) pairs up to produce
    // Vulkan-native behaviour — see docs/coord_system.md §4.
    static void flip_rows(uint8_t* dst, const uint8_t* src, uint32_t row_bytes, uint32_t rows) {
        for (uint32_t r = 0; r < rows; ++r) {
            std::memcpy(dst + r * row_bytes, src + (rows - 1 - r) * row_bytes, row_bytes);
        }
    }

    void OpenGLRenderDevice::upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip) {
        auto* tex = textures_.get(dst.id);
        if (!tex)
            return;

        auto fmt = gl::to_gl_format(tex->desc.format);
        uint32_t w = std::max(1u, tex->desc.width >> mip);
        uint32_t h = std::max(1u, tex->desc.height >> mip);
        uint32_t row_bytes = w * gl::pixel_bytes(tex->desc.format);

        // Y-flip the payload: user supplies row 0 = top of image (Vulkan
        // convention), but GL sampling is Y-flipped as well (see the GLSL
        // macro injection in create_shader), so storing the image upside
        // down here cancels out and gives v=0 = user's top on sampling.
        std::vector<uint8_t> flipped;
        const uint8_t* upload_ptr = data.data();
        if (row_bytes > 0 && h > 1 && data.size() >= static_cast<size_t>(row_bytes) * h) {
            flipped.resize(static_cast<size_t>(row_bytes) * h);
            flip_rows(flipped.data(), data.data(), row_bytes, h);
            upload_ptr = flipped.data();
        }

        glBindTexture(tex->target, tex->gl_id);
        glTexSubImage2D(tex->target, mip, 0, 0, w, h, fmt.format, fmt.type, upload_ptr);
        glBindTexture(tex->target, 0);
    }

    void OpenGLRenderDevice::upload_texture_region(TextureHandle dst,
                                                   uint32_t x,
                                                   uint32_t y,
                                                   uint32_t w,
                                                   uint32_t h,
                                                   std::span<const uint8_t> data,
                                                   uint32_t mip) {
        auto* tex = textures_.get(dst.id);
        if (!tex)
            return;

        auto fmt = gl::to_gl_format(tex->desc.format);
        uint32_t row_bytes = w * gl::pixel_bytes(tex->desc.format);

        // Flip the region's rows in place (so data's row 0 = top of the
        // sub-image ends up at the bottom of the GL upload) and translate
        // the destination Y from top-left to bottom-left framebuffer coords.
        std::vector<uint8_t> flipped;
        const uint8_t* upload_ptr = data.data();
        if (row_bytes > 0 && h > 1 && data.size() >= static_cast<size_t>(row_bytes) * h) {
            flipped.resize(static_cast<size_t>(row_bytes) * h);
            flip_rows(flipped.data(), data.data(), row_bytes, h);
            upload_ptr = flipped.data();
        }

        uint32_t tex_h = std::max(1u, tex->desc.height >> mip);
        uint32_t gl_y = (tex_h > y + h) ? (tex_h - y - h) : 0;

        glBindTexture(tex->target, tex->gl_id);
        glTexSubImage2D(tex->target,
                        mip,
                        static_cast<GLint>(x),
                        static_cast<GLint>(gl_y),
                        static_cast<GLsizei>(w),
                        static_cast<GLsizei>(h),
                        fmt.format,
                        fmt.type,
                        upload_ptr);
        glBindTexture(tex->target, 0);
    }

    // --- Readback ---

    void OpenGLRenderDevice::read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset) {
        auto* buf = buffers_.get(src.id);
        if (!buf)
            return;

        glBindBuffer(buf->target, buf->gl_id);
        void* mapped = glMapBufferRange(
            buf->target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size()), GL_MAP_READ_BIT);
        if (mapped) {
            std::memcpy(data.data(), mapped, data.size());
            glUnmapBuffer(buf->target);
        }
        glBindBuffer(buf->target, 0);
    }

    TextureDesc OpenGLRenderDevice::texture_desc(TextureHandle handle) const {
        auto it = textures_.get_const(handle.id);
        if (!it)
            return {};
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
        const uint32_t color_limit = std::min(TGFX2_MAX_COLOR_ATTACHMENTS, caps_.max_color_attachments);
        if (pass.colors.size() > color_limit) {
            tc::Log::error("OpenGLRenderDevice::get_or_create_fbo: %zu color attachments exceed limit %u",
                           pass.colors.size(),
                           color_limit);
            throw std::invalid_argument("OpenGL framebuffer color attachment count exceeds backend limit");
        }

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
                glFramebufferTexture2D(
                    GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i), tex->target, tex->gl_id, 0);
                has_color = true;
            }
        }

        if (pass.has_depth) {
            auto* tex = get_texture(pass.depth.texture);
            if (tex) {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex->target, tex->gl_id, 0);
            }
        }

        // Depth-only FBO (e.g. shadow map): disable color read/write
        if (!has_color) {
            gl_web_compat::set_draw_buffer(GL_NONE);
            glReadBuffer(GL_NONE);
        } else {
            std::array<GLenum, TGFX2_MAX_COLOR_ATTACHMENTS> draw_buffers{};
            for (size_t i = 0; i < pass.colors.size(); ++i) {
                draw_buffers[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
            }
            glDrawBuffers(static_cast<GLsizei>(pass.colors.size()), draw_buffers.data());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            // Diagnostic dump: which attachments were actually bound.
            std::string detail;
            char buf[128];
            std::snprintf(buf,
                          sizeof(buf),
                          "status=0x%04X colors=%zu has_depth=%d has_color_attached=%d",
                          status,
                          pass.colors.size(),
                          pass.has_depth ? 1 : 0,
                          has_color ? 1 : 0);
            detail = buf;
            for (size_t i = 0; i < pass.colors.size(); ++i) {
                auto* t = get_texture(pass.colors[i].texture);
                std::snprintf(buf,
                              sizeof(buf),
                              " color[%zu]={handle_id=%u tex=%s",
                              i,
                              pass.colors[i].texture.id,
                              t ? "found" : "NULL");
                detail += buf;
                if (t) {
                    std::snprintf(buf,
                                  sizeof(buf),
                                  " gl_id=%u target=0x%04X %ux%u samples=%u}",
                                  t->gl_id,
                                  t->target,
                                  t->desc.width,
                                  t->desc.height,
                                  t->desc.sample_count);
                    detail += buf;
                } else {
                    detail += "}";
                }
            }
            if (pass.has_depth) {
                auto* t = get_texture(pass.depth.texture);
                std::snprintf(
                    buf, sizeof(buf), " depth={handle_id=%u tex=%s", pass.depth.texture.id, t ? "found" : "NULL");
                detail += buf;
                if (t) {
                    std::snprintf(buf,
                                  sizeof(buf),
                                  " gl_id=%u target=0x%04X %ux%u samples=%u}",
                                  t->gl_id,
                                  t->target,
                                  t->desc.width,
                                  t->desc.height,
                                  t->desc.sample_count);
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

    void OpenGLRenderDevice::blit_to_texture(TextureHandle dst_color,
                                             TextureHandle src_color,
                                             termin::Bounds2i src_rect,
                                             termin::Bounds2i dst_rect) {
        GLTexture* src = textures_.get(src_color.id);
        GLTexture* dst = textures_.get(dst_color.id);
        if (!src || !dst) {
            tc_log_error("OpenGLRenderDevice::blit_to_texture: invalid texture handle "
                         "src=%u dst=%u",
                         src_color.id,
                         dst_color.id);
            return;
        }

        GLint prev_read = 0, prev_draw = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

        GLuint fbos[2] = {0, 0};
        glGenFramebuffers(2, fbos);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src->target, src->gl_id, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        GLenum read_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dst->target, dst->gl_id, 0);
        GLenum draw_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

        if (read_status != GL_FRAMEBUFFER_COMPLETE || draw_status != GL_FRAMEBUFFER_COMPLETE) {
            tc_log_error("OpenGLRenderDevice::blit_to_texture: incomplete framebuffer "
                         "read=0x%04x draw=0x%04x src_handle=%u dst_handle=%u "
                         "src_gl=%u dst_gl=%u src_target=0x%04x dst_target=0x%04x",
                         static_cast<unsigned>(read_status),
                         static_cast<unsigned>(draw_status),
                         src_color.id,
                         dst_color.id,
                         src->gl_id,
                         dst->gl_id,
                         static_cast<unsigned>(src->target),
                         static_cast<unsigned>(dst->target));
        } else {
            glBlitFramebuffer(src_rect.x0,
                              src_rect.y0,
                              src_rect.x1,
                              src_rect.y1,
                              dst_rect.x0,
                              dst_rect.y0,
                              dst_rect.x1,
                              dst_rect.y1,
                              GL_COLOR_BUFFER_BIT,
                              GL_LINEAR);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                tc_log_error("OpenGLRenderDevice::blit_to_texture: glBlitFramebuffer "
                             "failed err=0x%04x src_handle=%u dst_handle=%u "
                             "src=%dx%d dst=%dx%d",
                             static_cast<unsigned>(err),
                             src_color.id,
                             dst_color.id,
                             src_rect.width(),
                             src_rect.height(),
                             dst_rect.width(),
                             dst_rect.height());
            }
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw));
        glDeleteFramebuffers(2, fbos);
    }

    void OpenGLRenderDevice::clear_texture(TextureHandle dst_color, termin::LinearColor color, termin::Bounds2i viewport) {
        GLTexture* dst = textures_.get(dst_color.id);
        if (!dst)
            return;

        GLint prev_draw = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);
        GLint prev_vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, prev_vp);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dst->target, dst->gl_id, 0);
        glViewport(viewport.x0, viewport.y0, viewport.width(), viewport.height());
        glClearColor(color.r, color.g, color.b, color.a);
        GLboolean prev_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glClear(GL_COLOR_BUFFER_BIT);

        glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
        glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw));
        glDeleteFramebuffers(1, &fbo);
    }

    void OpenGLRenderDevice::present_to_default_framebuffer(TextureHandle src_color, int dst_w, int dst_h) {
        GLTexture* src = textures_.get(src_color.id);
        if (!src || dst_w <= 0 || dst_h <= 0) {
            tc::Log::error("OpenGLRenderDevice::present_to_default_framebuffer: invalid src/size");
            return;
        }

        const int src_w = static_cast<int>(src->desc.width);
        const int src_h = static_cast<int>(src->desc.height);
        if (src_w <= 0 || src_h <= 0) {
            tc::Log::error("OpenGLRenderDevice::present_to_default_framebuffer: invalid src size %dx%d", src_w, src_h);
            return;
        }

        GLboolean was_scissor = glIsEnabled(GL_SCISSOR_TEST);
        const bool was_framebuffer_srgb = gl_web_compat::framebuffer_srgb_enabled();
        GLboolean color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        GLboolean depth_mask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

        GLint prev_read = 0;
        GLint prev_draw = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

        if (was_scissor)
            glDisable(GL_SCISSOR_TEST);
        // The window presenter supplies an already transformed sRGB8 texture.
        // This blit is raw transport; applying framebuffer sRGB conversion a
        // second time would destroy the target-domain dithering pattern.
        if (was_framebuffer_srgb)
            gl_web_compat::set_framebuffer_srgb(false);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);

        GLuint read_fbo = 0;
        glGenFramebuffers(1, &read_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src->target, src->gl_id, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl_web_compat::set_draw_buffer(GL_BACK);

        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glBlitFramebuffer(0, 0, src_w, src_h, 0, 0, dst_w, dst_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        } else {
            tc::Log::error("OpenGLRenderDevice::present_to_default_framebuffer: source framebuffer incomplete");
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw));
        glDeleteFramebuffers(1, &read_fbo);

        glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        glDepthMask(depth_mask);
        if (was_scissor)
            glEnable(GL_SCISSOR_TEST);
        if (was_framebuffer_srgb)
            gl_web_compat::set_framebuffer_srgb(true);
    }

    void OpenGLRenderDevice::reset_state() {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        glDisable(GL_BLEND);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        gl_operations_->apply_polygon_mode(GL_FILL);

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
    }

    void OpenGLRenderDevice::flush() {
        glFlush();
    }
    void OpenGLRenderDevice::finish() {
        glFinish();
    }

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
        if (push_ring_initialized_)
            return;

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
            tc::Log::error(
                "tgfx2: push constants payload %u bytes exceeds max %u", size, TGFX2_PUSH_CONSTANTS_MAX_BYTES);
            return -1;
        }

        ensure_push_ring();

        const GLintptr align = static_cast<GLintptr>(push_ring_alignment_);
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

    // --- Transient vertex ring ---
    //
    // Mirrors push_ring_: a persistent VBO the immediate-mode draw paths
    // sub-upload into. No alignment requirement on vertex buffers (unlike
    // UBOs), so the cursor advances by raw size. On overflow the whole
    // storage is orphaned via glBufferData(NULL) and the cursor rewinds —
    // same pattern, same stall avoidance.
    //
    // The HandlePool slot is allocated once; transient_vertex_buffer()
    // returns the same BufferHandle for the process lifetime. Destructor
    // takes care of releasing both the GL id and the slot.

    void OpenGLRenderDevice::ensure_transient_vb() {
        if (transient_vb_initialized_)
            return;

        glGenBuffers(1, &transient_vb_gl_);
        glBindBuffer(GL_ARRAY_BUFFER, transient_vb_gl_);
        glBufferData(GL_ARRAY_BUFFER, transient_vb_size_, nullptr, GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        GLBuffer b;
        b.gl_id = transient_vb_gl_;
        b.desc.size = static_cast<uint64_t>(transient_vb_size_);
        b.desc.usage = BufferUsage::Vertex;
        b.target = GL_ARRAY_BUFFER;
        b.external = false;
        transient_vb_handle_ = BufferHandle{buffers_.add(std::move(b))};

        transient_vb_offset_ = 0;
        transient_vb_initialized_ = true;
    }

    BufferHandle OpenGLRenderDevice::transient_vertex_buffer() {
        ensure_transient_vb();
        return transient_vb_handle_;
    }

    void OpenGLRenderDevice::transient_vertex_reset_frame() {
        transient_vb_offset_ = 0;
    }

    uint64_t OpenGLRenderDevice::transient_vertex_write(const void* data, uint32_t size) {
        if (!data || size == 0 || size > static_cast<uint32_t>(transient_vb_size_)) {
            return UINT64_MAX;
        }

        ensure_transient_vb();

        GLintptr offset = transient_vb_offset_;
        if (offset + static_cast<GLintptr>(size) > transient_vb_size_) {
            // Orphan + rewind.
            glBindBuffer(GL_ARRAY_BUFFER, transient_vb_gl_);
            glBufferData(GL_ARRAY_BUFFER, transient_vb_size_, nullptr, GL_STREAM_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            offset = 0;
        }

        glBindBuffer(GL_ARRAY_BUFFER, transient_vb_gl_);
        glBufferSubData(GL_ARRAY_BUFFER, offset, static_cast<GLsizeiptr>(size), data);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        transient_vb_offset_ = offset + static_cast<GLintptr>(size);
        return static_cast<uint64_t>(offset);
    }

    // --- Dynamic UBO ring ---
    //
    // Same ownership pattern as transient_vertex_buffer(): one GL buffer is
    // registered in the BufferHandle pool for command-list binding, while
    // writes sub-upload aligned ranges and rewind/orphan on overflow.

    void OpenGLRenderDevice::ensure_ring_ubo() {
        if (ring_ubo_initialized_)
            return;

        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ring_ubo_alignment_);
        if (ring_ubo_alignment_ <= 0) {
            ring_ubo_alignment_ = 256;
        }

        glGenBuffers(1, &ring_ubo_gl_);
        glBindBuffer(GL_UNIFORM_BUFFER, ring_ubo_gl_);
        glBufferData(GL_UNIFORM_BUFFER, ring_ubo_size_, nullptr, GL_STREAM_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        GLBuffer b;
        b.gl_id = ring_ubo_gl_;
        b.desc.size = static_cast<uint64_t>(ring_ubo_size_);
        b.desc.usage = BufferUsage::Uniform;
        b.target = GL_UNIFORM_BUFFER;
        b.external = false;
        ring_ubo_handle_ = BufferHandle{buffers_.add(std::move(b))};

        ring_ubo_offset_ = 0;
        ring_ubo_initialized_ = true;
    }

    void OpenGLRenderDevice::ring_ubo_reset_frame() {
        ring_ubo_offset_ = 0;
    }

    bool OpenGLRenderDevice::ring_ubo_write(const void* data, uint32_t size, uint32_t& out_offset) {
        if (!data || size == 0) {
            return false;
        }

        ensure_ring_ubo();

        if (size > static_cast<uint32_t>(ring_ubo_size_)) {
            tc::Log::error("OpenGLRenderDevice::ring_ubo_write: payload %u exceeds ring capacity %lld",
                           size,
                           static_cast<long long>(ring_ubo_size_));
            return false;
        }

        const GLintptr align = static_cast<GLintptr>(ring_ubo_alignment_);
        const GLintptr padded = (static_cast<GLintptr>(size) + align - 1) / align * align;

        GLintptr offset = (ring_ubo_offset_ + align - 1) / align * align;

        if (offset + padded > ring_ubo_size_) {
            glBindBuffer(GL_UNIFORM_BUFFER, ring_ubo_gl_);
            glBufferData(GL_UNIFORM_BUFFER, ring_ubo_size_, nullptr, GL_STREAM_DRAW);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
            offset = 0;
        }

        glBindBuffer(GL_UNIFORM_BUFFER, ring_ubo_gl_);
        glBufferSubData(GL_UNIFORM_BUFFER, offset, static_cast<GLsizeiptr>(size), data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        ring_ubo_offset_ = offset + padded;
        out_offset = static_cast<uint32_t>(offset);
        return true;
    }

} // namespace tgfx
