#include "line_renderer_common.hpp"

#include "tgfx2/builtin_shader_sources.hpp"

#include <limits>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

#include <tcbase/tc_log.hpp>

namespace tgfx::line_renderer {

bool ensure_shader_pair(IRenderDevice& device,
                        tc_shader_handle& registry_handle,
                        const char* uuid,
                        const char* label,
                        const char* owner_name,
                        ShaderHandle& vertex_shader,
                        ShaderHandle& fragment_shader) {
    if (vertex_shader && fragment_shader) {
        return true;
    }

    if (tc_shader_handle_is_invalid(registry_handle)) {
        registry_handle = register_builtin_shader_from_catalog(uuid);
    }
    if (tc_shader_handle_is_invalid(registry_handle)) {
        tc::Log::error("[%s] failed to register %s shader", owner_name, label);
        return false;
    }

    tc_shader* raw = tc_shader_get(registry_handle);
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, &device, &vertex_shader, &fragment_shader)) {
        tc::Log::error("[%s] failed to create %s shader", owner_name, label);
        vertex_shader = {};
        fragment_shader = {};
        return false;
    }
    return true;
}

bool ensure_fragment_shader(IRenderDevice& device,
                            tc_shader_handle& registry_handle,
                            const char* uuid,
                            const char* label,
                            const char* owner_name,
                            ShaderHandle& fragment_shader) {
    if (fragment_shader) {
        return true;
    }

    if (tc_shader_handle_is_invalid(registry_handle)) {
        registry_handle = register_builtin_shader_from_catalog(uuid);
    }
    if (tc_shader_handle_is_invalid(registry_handle)) {
        tc::Log::error("[%s] failed to register %s shader", owner_name, label);
        return false;
    }

    tc_shader* raw = tc_shader_get(registry_handle);
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, &device, nullptr, &fragment_shader)) {
        tc::Log::error("[%s] failed to create %s shader", owner_name, label);
        fragment_shader = {};
        return false;
    }
    return true;
}

bool same_point(LinePoint3 a, LinePoint3 b) {
    return (a - b).norm_squared() <= 1.0e-12f;
}

std::vector<LinePoint3> clean_points(std::span<const LinePoint3> points) {
    std::vector<LinePoint3> clean;
    clean.reserve(points.size());
    for (LinePoint3 point : points) {
        if (clean.empty() || !same_point(clean.back(), point)) {
            clean.push_back(point);
        }
    }
    return clean;
}

BufferHandle create_static_vertex_buffer(IRenderDevice& device,
                                         const void* data,
                                         size_t byte_size) {
    if (!data || byte_size == 0) {
        return {};
    }

    BufferDesc desc;
    desc.size = byte_size;
    desc.usage = BufferUsage::Vertex;
    BufferHandle buffer = device.create_buffer(desc);
    device.upload_buffer(
        buffer,
        {reinterpret_cast<const uint8_t*>(data), byte_size});
    return buffer;
}

UploadedInstanceStream upload_instance_stream(RenderContext2& ctx,
                                              const void* data,
                                              size_t byte_size) {
    UploadedInstanceStream stream;
    if (!data || byte_size == 0) {
        return stream;
    }

    IRenderDevice& device = ctx.device();
    if (byte_size <= std::numeric_limits<uint32_t>::max()) {
        const uint64_t ring_offset = device.transient_vertex_write(
            data, static_cast<uint32_t>(byte_size));
        if (ring_offset != UINT64_MAX) {
            stream.buffer = device.transient_vertex_buffer();
            stream.offset = ring_offset;
            return stream;
        }
    }

    stream.buffer = create_static_vertex_buffer(device, data, byte_size);
    ctx.defer_destroy(stream.buffer);
    return stream;
}

} // namespace tgfx::line_renderer
