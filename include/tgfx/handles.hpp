#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <string>

#include "tgfx/types.hpp"
#include "tgfx/frame_graph_resource.hpp"

namespace termin {

// Abstract shader program handle.
class ShaderHandle {
public:
    virtual ~ShaderHandle() = default;

    virtual void use() = 0;
    virtual void stop() = 0;
    virtual void release() = 0;

    virtual void set_uniform_int(const char* name, int value) = 0;
    virtual void set_uniform_float(const char* name, float value) = 0;
    virtual void set_uniform_vec2(const char* name, float x, float y) = 0;
    virtual void set_uniform_vec3(const char* name, float x, float y, float z) = 0;
    virtual void set_uniform_vec4(const char* name, float x, float y, float z, float w) = 0;
    virtual void set_uniform_matrix4(const char* name, const float* data, bool transpose = true) = 0;
    virtual void set_uniform_matrix4_array(const char* name, const float* data, int count, bool transpose = true) = 0;

    // UBO binding (for GLSL 330 compatibility - manual binding instead of layout(binding=N))
    virtual void set_uniform_block_binding(const char* block_name, int binding_point) = 0;
};

// Abstract mesh buffer handle (VAO/VBO/EBO).
class GPUMeshHandle {
public:
    virtual ~GPUMeshHandle() = default;

    virtual void draw() = 0;
    virtual void release() = 0;
};

// Abstract GPU texture handle.
class GPUTextureHandle {
public:
    virtual ~GPUTextureHandle() = default;

    virtual void bind(int unit = 0) = 0;
    virtual void release() = 0;

    virtual uint32_t get_id() const = 0;
    virtual int get_width() const = 0;
    virtual int get_height() const = 0;
};

// Abstract framebuffer handle.
class FramebufferHandle : public FrameGraphResource {
public:
    virtual ~FramebufferHandle() = default;

    const char* resource_type() const override { return "fbo"; }

    virtual void resize(int width, int height) = 0;
    virtual void release() = 0;

    // Rebind to an externally managed FBO (e.g., Qt's default framebuffer).
    virtual void set_external_target(uint32_t fbo_id, int width, int height) = 0;

    virtual uint32_t get_fbo_id() const = 0;
    virtual int get_width() const = 0;
    virtual int get_height() const = 0;
    virtual int get_samples() const = 0;
    virtual bool is_msaa() const = 0;
    virtual std::string get_format() const = 0;

    // Query actual GL parameters from the color texture (for debugging)
    virtual std::string get_actual_gl_format() const { return "unknown"; }
    virtual int get_actual_gl_width() const { return 0; }
    virtual int get_actual_gl_height() const { return 0; }
    virtual int get_actual_gl_samples() const { return 0; }

    // Filter mode (requested and actual)
    virtual std::string get_filter() const { return "unknown"; }
    virtual std::string get_actual_gl_filter() const { return "unknown"; }

    // Convenience methods
    Size2i get_size() const { return Size2i(get_width(), get_height()); }
    void resize(Size2i size) { resize(size.width, size.height); }
    void set_external_target(uint32_t fbo_id, Size2i size) {
        set_external_target(fbo_id, size.width, size.height);
    }

    virtual GPUTextureHandle* color_texture() = 0;
    virtual GPUTextureHandle* depth_texture() = 0;
};

// Abstract uniform buffer handle (UBO).
class UniformBufferHandle {
public:
    virtual ~UniformBufferHandle() = default;

    virtual void update(const void* data, size_t size, size_t offset = 0) = 0;
    virtual void bind(int binding_point) = 0;
    virtual void unbind() = 0;
    virtual void release() = 0;

    virtual uint32_t get_id() const = 0;
    virtual size_t get_size() const = 0;
};

// Unique pointer types for handles.
using ShaderHandlePtr = std::unique_ptr<ShaderHandle>;
using GPUMeshHandlePtr = std::unique_ptr<GPUMeshHandle>;
using GPUTextureHandlePtr = std::unique_ptr<GPUTextureHandle>;
using FramebufferHandlePtr = std::unique_ptr<FramebufferHandle>;
using UniformBufferHandlePtr = std::unique_ptr<UniformBufferHandle>;

} // namespace termin
