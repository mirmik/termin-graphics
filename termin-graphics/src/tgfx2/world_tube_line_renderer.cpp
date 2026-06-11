#include "tgfx2/world_tube_line_renderer.hpp"

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

struct TubeCornerVertex {
    float endpoint;
    float axis0;
    float axis1;
};

struct TubeCapCornerVertex {
    float center;
    float axis0;
    float axis1;
};

struct TubeSegmentInstance {
    float p0[3];
    float width;
    float p1[3];
    float pad;
    float color[4];
};

struct TubeCapInstance {
    float center[3];
    float width;
    float neighbor[3];
    float pad;
    float color[4];
};

struct TubePush {
    float view_projection[16];
    float up_hint[4];
};

constexpr float kPi = 3.14159265358979323846f;
constexpr const char* WORLD_TUBE_LINE_SHADER_UUID = "termin-engine-world-tube-line";
constexpr const char* WORLD_TUBE_LINE_CAP_SHADER_UUID =
    "termin-engine-world-tube-line-cap";
constexpr const char* WORLD_TUBE_LINE_LIT_SHADER_UUID =
    "termin-engine-world-tube-line-lit";

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
        tc::Log::error("[WorldTubeLineRenderer] failed to register %s shader", label);
        return false;
    }

    tc_shader* raw = tc_shader_get(registry_handle);
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, &device, &vertex_shader, &fragment_shader)) {
        tc::Log::error("[WorldTubeLineRenderer] failed to create %s shader", label);
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
        tc::Log::error("[WorldTubeLineRenderer] failed to register %s shader", label);
        return false;
    }

    tc_shader* raw = tc_shader_get(registry_handle);
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, &device, nullptr, &fragment_shader)) {
        tc::Log::error("[WorldTubeLineRenderer] failed to create %s shader", label);
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

std::vector<TubeCornerVertex> build_body_template(int sides) {
    sides = std::clamp(sides, 3, 32);
    std::vector<TubeCornerVertex> vertices;
    vertices.reserve(static_cast<size_t>(sides) * 6);

    auto ring = [sides](int index) {
        const float t = (2.0f * kPi * static_cast<float>(index))
            / static_cast<float>(sides);
        return std::array<float, 2>{std::cos(t), std::sin(t)};
    };

    for (int i = 0; i < sides; ++i) {
        const auto a = ring(i);
        const auto b = ring((i + 1) % sides);
        vertices.push_back({0.0f, a[0], a[1]});
        vertices.push_back({1.0f, b[0], b[1]});
        vertices.push_back({1.0f, a[0], a[1]});
        vertices.push_back({0.0f, a[0], a[1]});
        vertices.push_back({0.0f, b[0], b[1]});
        vertices.push_back({1.0f, b[0], b[1]});
    }
    return vertices;
}

std::vector<TubeCapCornerVertex> build_cap_template(int sides) {
    sides = std::clamp(sides, 3, 32);
    std::vector<TubeCapCornerVertex> vertices;
    vertices.reserve(static_cast<size_t>(sides) * 3);

    auto ring = [sides](int index) {
        const float t = (2.0f * kPi * static_cast<float>(index))
            / static_cast<float>(sides);
        return std::array<float, 2>{std::cos(t), std::sin(t)};
    };

    for (int i = 0; i < sides; ++i) {
        const auto a = ring(i);
        const auto b = ring((i + 1) % sides);
        vertices.push_back({1.0f, 0.0f, 0.0f});
        vertices.push_back({0.0f, b[0], b[1]});
        vertices.push_back({0.0f, a[0], a[1]});
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

} // namespace

void WorldTubeLineRenderer::ensure_resources(RenderContext2& ctx, int sides) {
    sides = std::clamp(sides, 3, 32);
    IRenderDevice& device = ctx.device();

    if (template_sides_ != sides) {
        if (body_corner_vbo_) {
            device.destroy(body_corner_vbo_);
            body_corner_vbo_ = {};
        }
        if (cap_corner_vbo_) {
            device.destroy(cap_corner_vbo_);
            cap_corner_vbo_ = {};
        }
        body_corner_count_ = 0;
        cap_corner_count_ = 0;
        template_sides_ = sides;
    }

    if (!body_corner_vbo_) {
        std::vector<TubeCornerVertex> vertices = build_body_template(sides);
        body_corner_count_ = static_cast<uint32_t>(vertices.size());
        BufferDesc desc;
        desc.size = sizeof(TubeCornerVertex) * vertices.size();
        desc.usage = BufferUsage::Vertex;
        body_corner_vbo_ = device.create_buffer(desc);
        device.upload_buffer(
            body_corner_vbo_,
            {reinterpret_cast<const uint8_t*>(vertices.data()), desc.size});
    }

    if (!cap_corner_vbo_) {
        std::vector<TubeCapCornerVertex> vertices = build_cap_template(sides);
        cap_corner_count_ = static_cast<uint32_t>(vertices.size());
        BufferDesc desc;
        desc.size = sizeof(TubeCapCornerVertex) * vertices.size();
        desc.usage = BufferUsage::Vertex;
        cap_corner_vbo_ = device.create_buffer(desc);
        device.upload_buffer(
            cap_corner_vbo_,
            {reinterpret_cast<const uint8_t*>(vertices.data()), desc.size});
    }

    ensure_shader_pair(
        device,
        body_shader_handle_,
        WORLD_TUBE_LINE_SHADER_UUID,
        "body",
        body_vertex_shader_,
        body_fragment_shader_);
    ensure_shader_pair(
        device,
        cap_shader_handle_,
        WORLD_TUBE_LINE_CAP_SHADER_UUID,
        "cap",
        cap_vertex_shader_,
        cap_fragment_shader_);
    ensure_fragment_shader(
        device,
        lit_shader_handle_,
        WORLD_TUBE_LINE_LIT_SHADER_UUID,
        "lit fragment",
        lit_fragment_shader_);
}

void WorldTubeLineRenderer::draw_polyline(
        RenderContext2& ctx,
        std::span<const LinePoint3> points,
        const WorldTubeLineStyle& style,
        const WorldTubeLineParams& params) {
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

    std::vector<TubeSegmentInstance> segments;
    std::vector<TubeCapInstance> caps;
    segments.reserve(clean_points.size() - 1);
    for (size_t i = 1; i < clean_points.size(); ++i) {
        const LinePoint3 p0 = clean_points[i - 1];
        const LinePoint3 p1 = clean_points[i];
        if (same_point(p0, p1)) {
            continue;
        }

        TubeSegmentInstance segment{};
        segment.p0[0] = p0.x;
        segment.p0[1] = p0.y;
        segment.p0[2] = p0.z;
        segment.width = style.width;
        segment.p1[0] = p1.x;
        segment.p1[1] = p1.y;
        segment.p1[2] = p1.z;
        std::memcpy(segment.color, style.color.data(), sizeof(segment.color));
        segments.push_back(segment);
    }

    if (segments.empty()) {
        return;
    }

    const LinePoint3 first = clean_points.front();
    const LinePoint3 second = clean_points[1];
    if (!same_point(first, second)) {
        TubeCapInstance cap{};
        cap.center[0] = first.x;
        cap.center[1] = first.y;
        cap.center[2] = first.z;
        cap.width = style.width;
        cap.neighbor[0] = second.x;
        cap.neighbor[1] = second.y;
        cap.neighbor[2] = second.z;
        std::memcpy(cap.color, style.color.data(), sizeof(cap.color));
        caps.push_back(cap);
    }

    const LinePoint3 last = clean_points.back();
    const LinePoint3 prev = clean_points[clean_points.size() - 2];
    if (!same_point(last, prev)) {
        TubeCapInstance cap{};
        cap.center[0] = last.x;
        cap.center[1] = last.y;
        cap.center[2] = last.z;
        cap.width = style.width;
        cap.neighbor[0] = prev.x;
        cap.neighbor[1] = prev.y;
        cap.neighbor[2] = prev.z;
        std::memcpy(cap.color, style.color.data(), sizeof(cap.color));
        caps.push_back(cap);
    }

    ensure_resources(ctx, style.sides);
    const ShaderHandle body_selected_fragment_shader = params.fragment_shader
        ? params.fragment_shader
        : (params.lighting_enabled ? lit_fragment_shader_ : body_fragment_shader_);
    if (!body_vertex_shader_ || !body_selected_fragment_shader) {
        return;
    }

    const UploadedInstanceStream segment_stream = upload_instance_stream(
        ctx,
        segments.data(),
        segments.size() * sizeof(TubeSegmentInstance));
    if (!segment_stream.buffer) {
        return;
    }

    TubePush push{};
    std::memcpy(push.view_projection,
                params.view_projection.data(),
                sizeof(push.view_projection));
    push.up_hint[0] = style.up_hint.x;
    push.up_hint[1] = style.up_hint.y;
    push.up_hint[2] = style.up_hint.z;
    push.up_hint[3] = 0.0f;

    VertexBufferLayout corners;
    corners.stride = sizeof(TubeCornerVertex);
    corners.per_instance = false;
    corners.attributes = {
        {0, VertexFormat::Float3, 0},
    };

    VertexBufferLayout segment_layout;
    segment_layout.stride = sizeof(TubeSegmentInstance);
    segment_layout.per_instance = true;
    segment_layout.attributes = {
        {1, VertexFormat::Float3, offsetof(TubeSegmentInstance, p0)},
        {2, VertexFormat::Float,  offsetof(TubeSegmentInstance, width)},
        {3, VertexFormat::Float3, offsetof(TubeSegmentInstance, p1)},
        {4, VertexFormat::Float4, offsetof(TubeSegmentInstance, color)},
    };

    ctx.use_shader_resource_layout(nullptr);
    ctx.bind_shader(body_vertex_shader_, body_selected_fragment_shader);
    ctx.set_vertex_layouts({corners, segment_layout});
    ctx.set_topology(PrimitiveTopology::TriangleList);
    ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
    ctx.draw_arrays_instanced(
        body_corner_vbo_,
        0,
        segment_stream.buffer,
        segment_stream.offset,
        body_corner_count_,
        static_cast<uint32_t>(segments.size()));

    if (caps.empty()) {
        return;
    }

    const UploadedInstanceStream cap_stream = upload_instance_stream(
        ctx,
        caps.data(),
        caps.size() * sizeof(TubeCapInstance));
    if (!cap_stream.buffer) {
        return;
    }

    VertexBufferLayout cap_corners;
    cap_corners.stride = sizeof(TubeCapCornerVertex);
    cap_corners.per_instance = false;
    cap_corners.attributes = {
        {0, VertexFormat::Float3, 0},
    };

    VertexBufferLayout cap_layout;
    cap_layout.stride = sizeof(TubeCapInstance);
    cap_layout.per_instance = true;
    cap_layout.attributes = {
        {1, VertexFormat::Float3, offsetof(TubeCapInstance, center)},
        {2, VertexFormat::Float,  offsetof(TubeCapInstance, width)},
        {3, VertexFormat::Float3, offsetof(TubeCapInstance, neighbor)},
        {4, VertexFormat::Float4, offsetof(TubeCapInstance, color)},
    };

    const ShaderHandle cap_selected_fragment_shader = params.fragment_shader
        ? params.fragment_shader
        : (params.lighting_enabled ? lit_fragment_shader_ : cap_fragment_shader_);
    if (!cap_vertex_shader_ || !cap_selected_fragment_shader) {
        return;
    }

    ctx.use_shader_resource_layout(nullptr);
    ctx.bind_shader(cap_vertex_shader_, cap_selected_fragment_shader);
    ctx.set_vertex_layouts({cap_corners, cap_layout});
    ctx.set_topology(PrimitiveTopology::TriangleList);
    ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
    ctx.draw_arrays_instanced(
        cap_corner_vbo_,
        0,
        cap_stream.buffer,
        cap_stream.offset,
        cap_corner_count_,
        static_cast<uint32_t>(caps.size()));
}

void WorldTubeLineRenderer::release(RenderContext2& ctx) {
    IRenderDevice& device = ctx.device();
    if (body_corner_vbo_) {
        device.destroy(body_corner_vbo_);
        body_corner_vbo_ = {};
        body_corner_count_ = 0;
    }
    if (cap_corner_vbo_) {
        device.destroy(cap_corner_vbo_);
        cap_corner_vbo_ = {};
        cap_corner_count_ = 0;
    }
    body_vertex_shader_ = {};
    body_fragment_shader_ = {};
    cap_vertex_shader_ = {};
    cap_fragment_shader_ = {};
    lit_fragment_shader_ = {};
    template_sides_ = 0;
}

} // namespace tgfx
