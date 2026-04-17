#pragma once

#include <tgfx/tgfx_api.h>
#include <glad/glad.h>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "tgfx/tgfx_gpu_ops.h"
#include "tgfx/tc_gpu_context.h"
}

#include <tcbase/tc_log.hpp>

namespace termin {

// Initialize OpenGL function pointers via glad.
// Must be called after OpenGL context is created.
// Returns true on success.
inline bool init_opengl() {
    return gladLoaderLoadGL() != 0;
}

// GL_KHR_debug constants (may not be in all glad versions)
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_DEBUG_SOURCE_API 0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM 0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY 0x8249
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#define GL_DEBUG_SOURCE_OTHER 0x824B
#define GL_DEBUG_TYPE_ERROR 0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#define GL_DEBUG_TYPE_MARKER 0x8268
#define GL_DEBUG_TYPE_OTHER 0x8251
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#define GL_DEBUG_SEVERITY_LOW 0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif

// OpenGL debug callback for detailed error messages.
// Enabled when GL_KHR_debug extension is available.
inline void GLAPIENTRY gl_debug_callback(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei /*length*/,
    const GLchar* message,
    const void* /*userParam*/
) {
    // Convert source to string
    const char* src_str = "UNKNOWN";
    switch (source) {
        case GL_DEBUG_SOURCE_API: src_str = "API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: src_str = "WINDOW"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: src_str = "SHADER"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY: src_str = "3RD_PARTY"; break;
        case GL_DEBUG_SOURCE_APPLICATION: src_str = "APP"; break;
        case GL_DEBUG_SOURCE_OTHER: src_str = "OTHER"; break;
    }

    // Convert type to string
    const char* type_str = "UNKNOWN";
    switch (type) {
        case GL_DEBUG_TYPE_ERROR: type_str = "ERROR"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_str = "DEPRECATED"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: type_str = "UNDEFINED"; break;
        case GL_DEBUG_TYPE_PORTABILITY: type_str = "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE: type_str = "PERFORMANCE"; break;
        case GL_DEBUG_TYPE_MARKER: type_str = "MARKER"; break;
        case GL_DEBUG_TYPE_OTHER: type_str = "OTHER"; break;
    }

    // Log based on severity
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            tc::Log::error("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            tc::Log::warn("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        case GL_DEBUG_SEVERITY_LOW:
            tc::Log::info("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        default:
            tc::Log::debug("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
    }
}



} // namespace termin
