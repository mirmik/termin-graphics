#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "tgfx2/handles.hpp"
#include "tgfx2/line_mesh_builder.hpp"

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tgfx {

class IRenderDevice;
class RenderContext2;

namespace line_renderer {

struct UploadedInstanceStream {
    BufferHandle buffer;
    uint64_t offset = 0;
};

bool ensure_shader_pair(IRenderDevice& device,
                        tc_shader_handle& registry_handle,
                        const char* uuid,
                        const char* label,
                        const char* owner_name,
                        ShaderHandle& vertex_shader,
                        ShaderHandle& fragment_shader);

bool ensure_fragment_shader(IRenderDevice& device,
                            tc_shader_handle& registry_handle,
                            const char* uuid,
                            const char* label,
                            const char* owner_name,
                            ShaderHandle& fragment_shader);

bool same_point(LinePoint3 a, LinePoint3 b);
std::vector<LinePoint3> clean_points(std::span<const LinePoint3> points);

BufferHandle create_static_vertex_buffer(IRenderDevice& device,
                                         const void* data,
                                         size_t byte_size);

UploadedInstanceStream upload_instance_stream(RenderContext2& ctx,
                                              const void* data,
                                              size_t byte_size);

} // namespace line_renderer
} // namespace tgfx
