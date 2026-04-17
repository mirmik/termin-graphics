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

    /// Check if the underlying GPU texture is still valid.
    virtual bool is_valid() const { return get_id() != 0; }
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
using UniformBufferHandlePtr = std::unique_ptr<UniformBufferHandle>;

} // namespace termin
