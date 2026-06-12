#include "tgfx2/world_space_line_renderer.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

#include <tcbase/tc_log.hpp>

namespace tgfx {
namespace {

struct CornerVertex {
    float endpoint;
    float side;
};

struct SegmentInstance {
    float p0[3];
    float width;
    float p1[3];
    float flags;
    float color[4];
};

struct CapCornerVertex {
    float forward;
    float side;
};

struct CapInstance {
    float center[3];
    float width;
    float neighbor[3];
    float flags;
    float color[4];
};

struct JoinCornerVertex {
    float corner;
};

struct JoinInstance {
    float prev[3];
    float width;
    float center[3];
    float flags;
    float next[3];
    float pad;
    float color[4];
};

struct WorldLinePush {
    float view_projection[16];
    float camera_position[4];
};

constexpr std::array<CornerVertex, 6> kSegmentCorners{{
    {0.0f,  1.0f},
    {0.0f, -1.0f},
    {1.0f, -1.0f},
    {0.0f,  1.0f},
    {1.0f, -1.0f},
    {1.0f,  1.0f},
}};

constexpr std::array<JoinCornerVertex, 3> kBevelJoinCorners{{
    {0.0f},
    {1.0f},
    {2.0f},
}};

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFlagExtendStart = 1.0f;
constexpr float kFlagExtendEnd = 2.0f;
constexpr const char* WORLD_LINE_SHADER_UUID = "termin-engine-world-line";
constexpr const char* WORLD_LINE_CAP_SHADER_UUID = "termin-engine-world-line-cap";
constexpr const char* WORLD_LINE_JOIN_SHADER_UUID = "termin-engine-world-line-join";
constexpr const char* WORLD_LINE_ROUND_JOIN_SHADER_UUID =
    "termin-engine-world-line-round-join";
constexpr const char* WORLD_LINE_LIT_SHADER_UUID = "termin-engine-world-line-lit";
constexpr const char* WORLD_LINE_DRAW_RESOURCE = "world_line_draw";

bool ensure_shader_pair(
    IRenderDevice& device,
    tc_shader_handle& registry_handle,
    const char* uuid,
    const char* label,
    ShaderHandle& vertex_shader,
    ShaderHandle& fragment_shader)
{
    if (vertex_shader && fragment_shader) {
        return true;
    }

    if (tc_shader_handle_is_invalid(registry_handle)) {
        registry_handle = register_builtin_shader_from_catalog(uuid);
    }
    if (tc_shader_handle_is_invalid(registry_handle)) {
        tc::Log::error("[WorldSpaceLineRenderer] failed to register %s shader", label);
        return false;
    }

    tc_shader* raw = tc_shader_get(registry_handle);
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, &device, &vertex_shader, &fragment_shader)) {
        tc::Log::error("[WorldSpaceLineRenderer] failed to create %s shader", label);
        vertex_shader = {};
        fragment_shader = {};
        return false;
    }
    return true;
}

bool ensure_fragment_shader(
    IRenderDevice& device,
    tc_shader_handle& registry_handle,
    const char* uuid,
    const char* label,
    ShaderHandle& fragment_shader)
{
    if (fragment_shader) {
        return true;
    }

    if (tc_shader_handle_is_invalid(registry_handle)) {
        registry_handle = register_builtin_shader_from_catalog(uuid);
    }
    if (tc_shader_handle_is_invalid(registry_handle)) {
        tc::Log::error("[WorldSpaceLineRenderer] failed to register %s shader", label);
        return false;
    }

    tc_shader* raw = tc_shader_get(registry_handle);
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, &device, nullptr, &fragment_shader)) {
        tc::Log::error("[WorldSpaceLineRenderer] failed to create %s shader", label);
        fragment_shader = {};
        return false;
    }
    return true;
}

bool same_point(LinePoint3 a, LinePoint3 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz <= 1.0e-12f;
}

std::vector<CapCornerVertex> build_round_cap_template(int round_segments) {
    round_segments = std::clamp(round_segments, 4, 64);
    std::vector<CapCornerVertex> vertices;
    vertices.reserve(static_cast<size_t>(round_segments) * 3);

    auto rim = [round_segments](int i) {
        const float t = -kPi * 0.5f
            + kPi * static_cast<float>(i) / static_cast<float>(round_segments);
        return CapCornerVertex{std::cos(t), std::sin(t)};
    };

    CapCornerVertex prev = rim(0);
    for (int i = 1; i <= round_segments; ++i) {
        CapCornerVertex next = rim(i);
        vertices.push_back({0.0f, 0.0f});
        vertices.push_back(prev);
        vertices.push_back(next);
        prev = next;
    }
    return vertices;
}

std::vector<CapCornerVertex> build_round_join_template(int round_segments) {
    round_segments = std::clamp(round_segments, 4, 64);
    std::vector<CapCornerVertex> vertices;
    vertices.reserve(static_cast<size_t>(round_segments) * 3);

    auto rim = [round_segments](int i) {
        const float t = kPi * 2.0f
            * static_cast<float>(i) / static_cast<float>(round_segments);
        return CapCornerVertex{std::cos(t), std::sin(t)};
    };

    CapCornerVertex prev = rim(0);
    for (int i = 1; i <= round_segments; ++i) {
        CapCornerVertex next = rim(i);
        vertices.push_back({0.0f, 0.0f});
        vertices.push_back(prev);
        vertices.push_back(next);
        prev = next;
    }
    return vertices;
}

struct UploadedInstanceStream {
    BufferHandle buffer;
    uint64_t offset = 0;
};

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

    BufferDesc desc;
    desc.size = byte_size;
    desc.usage = BufferUsage::Vertex;
    stream.buffer = device.create_buffer(desc);
    device.upload_buffer(
        stream.buffer,
        {reinterpret_cast<const uint8_t*>(data), byte_size});
    ctx.defer_destroy(stream.buffer);
    return stream;
}

void bind_world_line_shader(RenderContext2& ctx,
                            tc_shader_handle shader_handle,
                            ShaderHandle vertex_shader,
                            ShaderHandle fragment_shader,
                            const WorldLinePush& draw_data) {
    ctx.bind_shader(vertex_shader, fragment_shader);
    ctx.use_shader_resource_layout(tc_shader_get(shader_handle));
    ctx.bind_uniform_data(
        WORLD_LINE_DRAW_RESOURCE,
        &draw_data,
        static_cast<uint32_t>(sizeof(draw_data)));
}

} // namespace

void WorldSpaceLineRenderer::ensure_resources(RenderContext2& ctx) {
    IRenderDevice& device = ctx.device();

    if (!corner_vbo_) {
        BufferDesc desc;
        desc.size = sizeof(CornerVertex) * kSegmentCorners.size();
        desc.usage = BufferUsage::Vertex;
        corner_vbo_ = device.create_buffer(desc);
        device.upload_buffer(
            corner_vbo_,
            {reinterpret_cast<const uint8_t*>(kSegmentCorners.data()), desc.size});
    }

    if (!join_corner_vbo_) {
        BufferDesc desc;
        desc.size = sizeof(JoinCornerVertex) * kBevelJoinCorners.size();
        desc.usage = BufferUsage::Vertex;
        join_corner_vbo_ = device.create_buffer(desc);
        device.upload_buffer(
            join_corner_vbo_,
            {reinterpret_cast<const uint8_t*>(kBevelJoinCorners.data()), desc.size});
    }

    ensure_shader_pair(
        device,
        shader_handle_,
        WORLD_LINE_SHADER_UUID,
        "segment",
        vertex_shader_,
        fragment_shader_);
    ensure_shader_pair(
        device,
        cap_shader_handle_,
        WORLD_LINE_CAP_SHADER_UUID,
        "cap",
        cap_vertex_shader_,
        cap_fragment_shader_);
    ensure_shader_pair(
        device,
        join_shader_handle_,
        WORLD_LINE_JOIN_SHADER_UUID,
        "join",
        join_vertex_shader_,
        join_fragment_shader_);
    ensure_shader_pair(
        device,
        round_join_shader_handle_,
        WORLD_LINE_ROUND_JOIN_SHADER_UUID,
        "round join",
        round_join_vertex_shader_,
        round_join_fragment_shader_);
    ensure_fragment_shader(
        device,
        lit_shader_handle_,
        WORLD_LINE_LIT_SHADER_UUID,
        "lit fragment",
        lit_fragment_shader_);
}

void WorldSpaceLineRenderer::ensure_cap_template(RenderContext2& ctx,
                                                 int round_segments) {
    round_segments = std::clamp(round_segments, 4, 64);
    if (cap_corner_vbo_ && cap_round_segments_ == round_segments) {
        return;
    }

    IRenderDevice& device = ctx.device();
    if (cap_corner_vbo_) {
        device.destroy(cap_corner_vbo_);
        cap_corner_vbo_ = {};
    }

    std::vector<CapCornerVertex> corners = build_round_cap_template(round_segments);
    cap_corner_count_ = static_cast<uint32_t>(corners.size());
    cap_round_segments_ = round_segments;

    BufferDesc desc;
    desc.size = sizeof(CapCornerVertex) * corners.size();
    desc.usage = BufferUsage::Vertex;
    cap_corner_vbo_ = device.create_buffer(desc);
    device.upload_buffer(
        cap_corner_vbo_,
        {reinterpret_cast<const uint8_t*>(corners.data()), desc.size});
}

void WorldSpaceLineRenderer::ensure_round_join_template(RenderContext2& ctx,
                                                        int round_segments) {
    round_segments = std::clamp(round_segments, 4, 64);
    if (round_join_corner_vbo_ && round_join_segments_ == round_segments) {
        return;
    }

    IRenderDevice& device = ctx.device();
    if (round_join_corner_vbo_) {
        device.destroy(round_join_corner_vbo_);
        round_join_corner_vbo_ = {};
    }

    std::vector<CapCornerVertex> corners = build_round_join_template(round_segments);
    round_join_corner_count_ = static_cast<uint32_t>(corners.size());
    round_join_segments_ = round_segments;

    BufferDesc desc;
    desc.size = sizeof(CapCornerVertex) * corners.size();
    desc.usage = BufferUsage::Vertex;
    round_join_corner_vbo_ = device.create_buffer(desc);
    device.upload_buffer(
        round_join_corner_vbo_,
        {reinterpret_cast<const uint8_t*>(corners.data()), desc.size});
}

void WorldSpaceLineRenderer::draw_polyline(
        RenderContext2& ctx,
        std::span<const LinePoint3> points,
        const WorldSpaceLineStyle& style,
        const WorldSpaceLineParams& params) {
    if (points.size() < 2 || style.width <= 0.0f) {
        return;
    }

    std::vector<LinePoint3> clean_points;
    clean_points.reserve(points.size());
    for (LinePoint3 point : points) {
        if (clean_points.empty() || !same_point(clean_points.back(), point)) {
            clean_points.push_back(point);
        }
    }

    if (clean_points.size() < 2) {
        return;
    }

    std::vector<SegmentInstance> instances;
    std::vector<CapInstance> cap_instances;
    std::vector<CapInstance> round_join_instances;
    std::vector<JoinInstance> join_instances;
    instances.reserve(clean_points.size() - 1);
    for (size_t i = 1; i < clean_points.size(); ++i) {
        const LinePoint3 p0 = clean_points[i - 1];
        const LinePoint3 p1 = clean_points[i];
        if (same_point(p0, p1)) {
            continue;
        }

        SegmentInstance instance{};
        instance.p0[0] = p0.x;
        instance.p0[1] = p0.y;
        instance.p0[2] = p0.z;
        instance.width = style.width;
        instance.p1[0] = p1.x;
        instance.p1[1] = p1.y;
        instance.p1[2] = p1.z;
        if (style.cap == LineCapStyle::Square && i == 1) {
            instance.flags += kFlagExtendStart;
        }
        if (style.cap == LineCapStyle::Square && i + 1 == clean_points.size()) {
            instance.flags += kFlagExtendEnd;
        }
        std::memcpy(instance.color, style.color.data(), sizeof(instance.color));
        instances.push_back(instance);
    }

    if (instances.empty()) {
        return;
    }

    if (style.cap == LineCapStyle::Round) {
        const LinePoint3 first = clean_points.front();
        const LinePoint3 second = clean_points[1];
        if (!same_point(first, second)) {
            CapInstance cap{};
            cap.center[0] = first.x;
            cap.center[1] = first.y;
            cap.center[2] = first.z;
            cap.width = style.width;
            cap.neighbor[0] = second.x;
            cap.neighbor[1] = second.y;
            cap.neighbor[2] = second.z;
            std::memcpy(cap.color, style.color.data(), sizeof(cap.color));
            cap_instances.push_back(cap);
        }

        const LinePoint3 last = clean_points.back();
        const LinePoint3 prev = clean_points[clean_points.size() - 2];
        if (!same_point(last, prev)) {
            CapInstance cap{};
            cap.center[0] = last.x;
            cap.center[1] = last.y;
            cap.center[2] = last.z;
            cap.width = style.width;
            cap.neighbor[0] = prev.x;
            cap.neighbor[1] = prev.y;
            cap.neighbor[2] = prev.z;
            std::memcpy(cap.color, style.color.data(), sizeof(cap.color));
            cap_instances.push_back(cap);
        }
    }

    if (style.join == LineJoinStyle::Round) {
        round_join_instances.reserve(
            clean_points.size() > 2 ? clean_points.size() - 2 : 0);
        for (size_t i = 1; i + 1 < clean_points.size(); ++i) {
            const LinePoint3 center = clean_points[i];
            const LinePoint3 next = clean_points[i + 1];
            CapInstance join{};
            join.center[0] = center.x;
            join.center[1] = center.y;
            join.center[2] = center.z;
            join.width = style.width;
            join.neighbor[0] = next.x;
            join.neighbor[1] = next.y;
            join.neighbor[2] = next.z;
            std::memcpy(join.color, style.color.data(), sizeof(join.color));
            round_join_instances.push_back(join);
        }
    } else if (style.join == LineJoinStyle::Bevel) {
        join_instances.reserve(clean_points.size() > 2 ? clean_points.size() - 2 : 0);
        for (size_t i = 1; i + 1 < clean_points.size(); ++i) {
            const LinePoint3 prev = clean_points[i - 1];
            const LinePoint3 center = clean_points[i];
            const LinePoint3 next = clean_points[i + 1];
            JoinInstance join{};
            join.prev[0] = prev.x;
            join.prev[1] = prev.y;
            join.prev[2] = prev.z;
            join.width = style.width;
            join.center[0] = center.x;
            join.center[1] = center.y;
            join.center[2] = center.z;
            join.next[0] = next.x;
            join.next[1] = next.y;
            join.next[2] = next.z;
            std::memcpy(join.color, style.color.data(), sizeof(join.color));
            join_instances.push_back(join);
        }
    }

    ensure_resources(ctx);
    const ShaderHandle segment_fragment_shader = params.fragment_shader
        ? params.fragment_shader
        : (params.lighting_enabled ? lit_fragment_shader_ : fragment_shader_);
    if (!vertex_shader_ || !segment_fragment_shader) {
        return;
    }

    const UploadedInstanceStream segment_stream = upload_instance_stream(
        ctx,
        instances.data(),
        instances.size() * sizeof(SegmentInstance));
    if (!segment_stream.buffer) {
        return;
    }

    WorldLinePush push{};
    std::memcpy(push.view_projection,
                params.view_projection.data(),
                sizeof(push.view_projection));
    push.camera_position[0] = params.camera_position[0];
    push.camera_position[1] = params.camera_position[1];
    push.camera_position[2] = params.camera_position[2];
    push.camera_position[3] = 1.0f;

    VertexBufferLayout corners;
    corners.stride = sizeof(CornerVertex);
    corners.per_instance = false;
    corners.use_shader_input_locations = true;
    corners.attributes = {
        {0, VertexFormat::Float2, 0},
    };

    VertexBufferLayout segment;
    segment.stride = sizeof(SegmentInstance);
    segment.per_instance = true;
    segment.use_shader_input_locations = true;
    segment.attributes = {
        {1, VertexFormat::Float3, offsetof(SegmentInstance, p0)},
        {2, VertexFormat::Float,  offsetof(SegmentInstance, width)},
        {3, VertexFormat::Float3, offsetof(SegmentInstance, p1)},
        {4, VertexFormat::Float,  offsetof(SegmentInstance, flags)},
        {5, VertexFormat::Float4, offsetof(SegmentInstance, color)},
    };

    bind_world_line_shader(ctx, shader_handle_, vertex_shader_, segment_fragment_shader, push);
    ctx.set_vertex_layouts({corners, segment});
    ctx.set_topology(PrimitiveTopology::TriangleList);
    ctx.draw_arrays_instanced(
        corner_vbo_,
        0,
        segment_stream.buffer,
        segment_stream.offset,
        static_cast<uint32_t>(kSegmentCorners.size()),
        static_cast<uint32_t>(instances.size()));

    if (!cap_instances.empty()) {
        ensure_cap_template(ctx, style.round_segments);
        const UploadedInstanceStream cap_stream = upload_instance_stream(
            ctx,
            cap_instances.data(),
            cap_instances.size() * sizeof(CapInstance));
        if (!cap_stream.buffer) {
            return;
        }

        VertexBufferLayout cap_corners;
        cap_corners.stride = sizeof(CapCornerVertex);
        cap_corners.per_instance = false;
        cap_corners.use_shader_input_locations = true;
        cap_corners.attributes = {
            {0, VertexFormat::Float2, 0},
        };

        VertexBufferLayout cap_layout;
        cap_layout.stride = sizeof(CapInstance);
        cap_layout.per_instance = true;
        cap_layout.use_shader_input_locations = true;
        cap_layout.attributes = {
            {1, VertexFormat::Float3, offsetof(CapInstance, center)},
            {2, VertexFormat::Float,  offsetof(CapInstance, width)},
            {3, VertexFormat::Float3, offsetof(CapInstance, neighbor)},
            {4, VertexFormat::Float,  offsetof(CapInstance, flags)},
            {5, VertexFormat::Float4, offsetof(CapInstance, color)},
        };

        const ShaderHandle cap_selected_fragment_shader = params.fragment_shader
            ? params.fragment_shader
            : (params.lighting_enabled ? lit_fragment_shader_ : cap_fragment_shader_);
        if (!cap_vertex_shader_ || !cap_selected_fragment_shader) {
            return;
        }

        bind_world_line_shader(
            ctx,
            cap_shader_handle_,
            cap_vertex_shader_,
            cap_selected_fragment_shader,
            push);
        ctx.set_vertex_layouts({cap_corners, cap_layout});
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.draw_arrays_instanced(
            cap_corner_vbo_,
            0,
            cap_stream.buffer,
            cap_stream.offset,
            cap_corner_count_,
            static_cast<uint32_t>(cap_instances.size()));
    }

    if (!round_join_instances.empty()) {
        ensure_round_join_template(ctx, style.round_segments);
        const UploadedInstanceStream round_join_stream = upload_instance_stream(
            ctx,
            round_join_instances.data(),
            round_join_instances.size() * sizeof(CapInstance));
        if (!round_join_stream.buffer) {
            return;
        }

        VertexBufferLayout round_join_corners;
        round_join_corners.stride = sizeof(CapCornerVertex);
        round_join_corners.per_instance = false;
        round_join_corners.use_shader_input_locations = true;
        round_join_corners.attributes = {
            {0, VertexFormat::Float2, 0},
        };

        VertexBufferLayout round_join_layout;
        round_join_layout.stride = sizeof(CapInstance);
        round_join_layout.per_instance = true;
        round_join_layout.use_shader_input_locations = true;
        round_join_layout.attributes = {
            {1, VertexFormat::Float3, offsetof(CapInstance, center)},
            {2, VertexFormat::Float,  offsetof(CapInstance, width)},
            {3, VertexFormat::Float3, offsetof(CapInstance, neighbor)},
            {4, VertexFormat::Float,  offsetof(CapInstance, flags)},
            {5, VertexFormat::Float4, offsetof(CapInstance, color)},
        };

        const ShaderHandle round_join_selected_fragment_shader = params.fragment_shader
            ? params.fragment_shader
            : (params.lighting_enabled ? lit_fragment_shader_ : round_join_fragment_shader_);
        if (!round_join_vertex_shader_ || !round_join_selected_fragment_shader) {
            return;
        }

        bind_world_line_shader(
            ctx,
            round_join_shader_handle_,
            round_join_vertex_shader_,
            round_join_selected_fragment_shader,
            push);
        ctx.set_vertex_layouts({round_join_corners, round_join_layout});
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.draw_arrays_instanced(
            round_join_corner_vbo_,
            0,
            round_join_stream.buffer,
            round_join_stream.offset,
            round_join_corner_count_,
            static_cast<uint32_t>(round_join_instances.size()));
    }

    if (!join_instances.empty()) {
        const UploadedInstanceStream join_stream = upload_instance_stream(
            ctx,
            join_instances.data(),
            join_instances.size() * sizeof(JoinInstance));
        if (!join_stream.buffer) {
            return;
        }

        VertexBufferLayout join_corners;
        join_corners.stride = sizeof(JoinCornerVertex);
        join_corners.per_instance = false;
        join_corners.use_shader_input_locations = true;
        join_corners.attributes = {
            {0, VertexFormat::Float, 0},
        };

        VertexBufferLayout join_layout;
        join_layout.stride = sizeof(JoinInstance);
        join_layout.per_instance = true;
        join_layout.use_shader_input_locations = true;
        join_layout.attributes = {
            {1, VertexFormat::Float3, offsetof(JoinInstance, prev)},
            {2, VertexFormat::Float,  offsetof(JoinInstance, width)},
            {3, VertexFormat::Float3, offsetof(JoinInstance, center)},
            {4, VertexFormat::Float,  offsetof(JoinInstance, flags)},
            {5, VertexFormat::Float3, offsetof(JoinInstance, next)},
            {6, VertexFormat::Float4, offsetof(JoinInstance, color)},
        };

        const ShaderHandle join_selected_fragment_shader = params.fragment_shader
            ? params.fragment_shader
            : (params.lighting_enabled ? lit_fragment_shader_ : join_fragment_shader_);
        if (!join_vertex_shader_ || !join_selected_fragment_shader) {
            return;
        }

        bind_world_line_shader(
            ctx,
            join_shader_handle_,
            join_vertex_shader_,
            join_selected_fragment_shader,
            push);
        ctx.set_vertex_layouts({join_corners, join_layout});
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.draw_arrays_instanced(
            join_corner_vbo_,
            0,
            join_stream.buffer,
            join_stream.offset,
            static_cast<uint32_t>(kBevelJoinCorners.size()),
            static_cast<uint32_t>(join_instances.size()));
    }
}

void WorldSpaceLineRenderer::release(RenderContext2& ctx) {
    IRenderDevice& device = ctx.device();
    if (corner_vbo_) {
        device.destroy(corner_vbo_);
        corner_vbo_ = {};
    }
    if (cap_corner_vbo_) {
        device.destroy(cap_corner_vbo_);
        cap_corner_vbo_ = {};
        cap_corner_count_ = 0;
        cap_round_segments_ = 0;
    }
    if (join_corner_vbo_) {
        device.destroy(join_corner_vbo_);
        join_corner_vbo_ = {};
    }
    if (round_join_corner_vbo_) {
        device.destroy(round_join_corner_vbo_);
        round_join_corner_vbo_ = {};
        round_join_corner_count_ = 0;
        round_join_segments_ = 0;
    }
    vertex_shader_ = {};
    fragment_shader_ = {};
    cap_vertex_shader_ = {};
    cap_fragment_shader_ = {};
    join_vertex_shader_ = {};
    join_fragment_shader_ = {};
    round_join_vertex_shader_ = {};
    round_join_fragment_shader_ = {};
    lit_fragment_shader_ = {};
}

} // namespace tgfx
