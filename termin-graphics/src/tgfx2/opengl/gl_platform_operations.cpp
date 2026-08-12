#include "tgfx2/opengl/gl_platform_operations.hpp"

#include <cstring>
#include <sstream>

#include <tcbase/tc_log.hpp>

#ifndef GL_UPPER_LEFT
#define GL_UPPER_LEFT 0x8CA2
#endif
#ifndef GL_ZERO_TO_ONE
#define GL_ZERO_TO_ONE 0x935F
#endif
#ifndef GL_CLIP_ORIGIN
#define GL_CLIP_ORIGIN 0x935C
#endif
#ifndef GL_CLIP_DEPTH_MODE
#define GL_CLIP_DEPTH_MODE 0x935D
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
    using ClipControlProc = void(APIENTRY*)(GLenum, GLenum);
    ClipControlProc clip_control_proc = nullptr;

    bool load_clip_control() {
        if (clip_control_proc)
            return true;
        PROC proc = wglGetProcAddress("glClipControl");
        if (!proc || proc == reinterpret_cast<PROC>(1) || proc == reinterpret_cast<PROC>(2) ||
            proc == reinterpret_cast<PROC>(3) || proc == reinterpret_cast<PROC>(-1)) {
            return false;
        }
        clip_control_proc = reinterpret_cast<ClipControlProc>(proc);
        return true;
    }
} // namespace
#elif !defined(__EMSCRIPTEN__)
extern "C" void glClipControl(GLenum origin, GLenum depth);
#endif

namespace tgfx {

    namespace {

        bool has_extension(const char* expected) {
            if (!glGetStringi)
                return false;
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (GLint index = 0; index < count; ++index) {
                const auto* extension = reinterpret_cast<const char*>(
                    glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
                if (extension && std::strcmp(extension, expected) == 0)
                    return true;
            }
            return false;
        }

        const char* tier_name(const GlFeatureSet& features) {
            return gl_feature_tier_name(features.tier);
        }

    } // namespace

    bool GlPlatformOperations::runtime_has_clip_control(uint32_t major, uint32_t minor) {
#if defined(__EMSCRIPTEN__)
        (void)major;
        (void)minor;
        return false;
#else
        const bool core_45 = major > 4 || (major == 4 && minor >= 5);
        if (!core_45 && !has_extension("GL_ARB_clip_control"))
            return false;
#if defined(_WIN32)
        return load_clip_control();
#else
        return true;
#endif
#endif
    }

    bool GlPlatformOperations::apply_clip_space_contract(std::string& error) const {
        error.clear();
        if (!features_.uses_clip_control)
            return true;

        for (GLenum stale = glGetError(); stale != GL_NO_ERROR; stale = glGetError()) {
            tc::Log::warn("GL clip-space setup observed a pre-existing error: 0x%04x",
                          static_cast<unsigned>(stale));
        }

#if defined(_WIN32)
        if (!load_clip_control()) {
            error = "wglGetProcAddress(glClipControl) failed";
            tc::Log::error("GL clip-space contract failed: %s", error.c_str());
            return false;
        }
        clip_control_proc(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
#elif !defined(__EMSCRIPTEN__)
        glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
#else
        error = "clip-control is unavailable in WebGL 2";
        tc::Log::error("GL clip-space contract failed: %s", error.c_str());
        return false;
#endif

        const GLenum apply_error = glGetError();
        GLint origin = 0;
        GLint depth_mode = 0;
        glGetIntegerv(GL_CLIP_ORIGIN, &origin);
        glGetIntegerv(GL_CLIP_DEPTH_MODE, &depth_mode);
        const GLenum query_error = glGetError();
        if (apply_error == GL_NO_ERROR && query_error == GL_NO_ERROR && origin == GL_UPPER_LEFT &&
            depth_mode == GL_ZERO_TO_ONE) {
            return true;
        }

        std::ostringstream message;
        message << "apply_error=0x" << std::hex << static_cast<unsigned>(apply_error)
                << " query_error=0x" << static_cast<unsigned>(query_error) << " origin=0x"
                << static_cast<unsigned>(origin) << " depth_mode=0x" << static_cast<unsigned>(depth_mode);
        error = message.str();
        tc::Log::error("GL clip-space contract failed: %s", error.c_str());
        return false;
    }

    bool GlPlatformOperations::apply_polygon_mode(GLenum mode) const {
        if (features_.supports_polygon_mode) {
            glPolygonMode(GL_FRONT_AND_BACK, mode);
            return true;
        }
        // WebGL rasterization is permanently fill mode, so requesting Fill is
        // already satisfied without an entry-point call.
        if (mode == GL_FILL)
            return true;
        tc::Log::error("GL tier '%s' does not support polygon mode 0x%04x",
                       tier_name(features_),
                       static_cast<unsigned>(mode));
        return false;
    }

    bool GlPlatformOperations::draw_elements(GLenum topology,
                                              GLsizei index_count,
                                              GLenum index_type,
                                              const void* index_offset,
                                              GLint base_vertex) const {
        if (base_vertex == 0) {
            glDrawElements(topology, index_count, index_type, index_offset);
            return true;
        }
        if (!features_.supports_base_vertex_draws) {
            tc::Log::error("GL tier '%s' cannot draw with base_vertex=%d", tier_name(features_), base_vertex);
            return false;
        }
        glDrawElementsBaseVertex(topology, index_count, index_type, index_offset, base_vertex);
        return true;
    }

    bool GlPlatformOperations::draw_elements_instanced(GLenum topology,
                                                        GLsizei index_count,
                                                        GLenum index_type,
                                                        const void* index_offset,
                                                        GLsizei instance_count,
                                                        GLint base_vertex) const {
        if (base_vertex == 0) {
            glDrawElementsInstanced(topology, index_count, index_type, index_offset, instance_count);
            return true;
        }
        if (!features_.supports_base_vertex_draws) {
            tc::Log::error("GL tier '%s' cannot draw instanced with base_vertex=%d",
                           tier_name(features_),
                           base_vertex);
            return false;
        }
        glDrawElementsInstancedBaseVertex(
            topology, index_count, index_type, index_offset, instance_count, base_vertex);
        return true;
    }

    bool GlPlatformOperations::allocate_multisample_texture_2d(GLenum target,
                                                                GLsizei samples,
                                                                GLenum internal_format,
                                                                GLsizei width,
                                                                GLsizei height,
                                                                bool fixed_sample_locations) const {
        if (!features_.supports_multisample_textures) {
            tc::Log::error("GL tier '%s' does not support multisample texture allocation", tier_name(features_));
            return false;
        }
        glTexImage2DMultisample(target,
                                samples,
                                internal_format,
                                width,
                                height,
                                fixed_sample_locations ? GL_TRUE : GL_FALSE);
        return true;
    }

    bool GlPlatformOperations::issue_timestamp(GLuint query) const {
        if (!features_.supports_timestamp_queries) {
            tc::Log::error("GL tier '%s' does not support timestamp queries", tier_name(features_));
            return false;
        }
        glQueryCounter(query, GL_TIMESTAMP);
        return true;
    }

    bool GlPlatformOperations::read_timestamp(GLuint query,
                                               bool wait,
                                               uint64_t& timestamp,
                                               bool& available) const {
        timestamp = 0;
        available = false;
        if (!features_.supports_timestamp_queries) {
            tc::Log::error("GL tier '%s' does not support timestamp query results", tier_name(features_));
            return false;
        }
        if (!wait) {
            GLint ready = GL_FALSE;
            glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready != GL_TRUE)
                return true;
        }
        GLuint64 value = 0;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &value);
        timestamp = static_cast<uint64_t>(value);
        available = true;
        return true;
    }

} // namespace tgfx
