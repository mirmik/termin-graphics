#include "tgfx2/screen_space_line_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

namespace tgfx {
namespace {

struct CornerVertex {
    float endpoint;
    float side;
};

struct SegmentInstance {
    float p0[3];
    float width_px;
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
    float width_px;
    float neighbor[3];
    float flags;
    float color[4];
};

struct JoinCornerVertex {
    float corner;
};

struct JoinInstance {
    float prev[3];
    float width_px;
    float center[3];
    float flags;
    float next[3];
    float pad;
    float color[4];
};

struct ScreenLinePush {
    float view_projection[16];
    float viewport[4];
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

constexpr const char* kScreenLineCommon = R"(
struct ScreenLinePush {
    mat4 u_view_projection;
    vec4 u_viewport;
};
#ifdef VULKAN
layout(push_constant) uniform ScreenLinePushBlock { ScreenLinePush pc; };
#else
layout(std140, binding = 14) uniform ScreenLinePushBlock { ScreenLinePush pc; };
#endif
)";

std::string make_screen_line_vert() {
    return std::string("#version 450 core\n") + kScreenLineCommon + R"(
layout(location=0) in vec2 a_corner;
layout(location=1) in vec3 a_p0;
layout(location=2) in float a_width_px;
layout(location=3) in vec3 a_p1;
layout(location=4) in float a_flags;
layout(location=5) in vec4 a_color;

layout(location=0) out vec4 v_color;

void main() {
    vec4 c0 = pc.u_view_projection * vec4(a_p0, 1.0);
    vec4 c1 = pc.u_view_projection * vec4(a_p1, 1.0);

    float endpoint = a_corner.x;
    float side_sign = a_corner.y;

    vec2 viewport = max(pc.u_viewport.xy, vec2(1.0));
    vec2 ndc0 = c0.xy / c0.w;
    vec2 ndc1 = c1.xy / c1.w;
    vec2 px0 = (ndc0 * 0.5 + 0.5) * viewport;
    vec2 px1 = (ndc1 * 0.5 + 0.5) * viewport;

    vec2 dir = px1 - px0;
    float len = length(dir);
    if (len < 1.0e-5) {
        dir = vec2(1.0, 0.0);
    } else {
        dir /= len;
    }

    vec2 side = vec2(-dir.y, dir.x);
    vec2 base_px = mix(px0, px1, endpoint);
    bool extend_start = mod(a_flags, 2.0) >= 1.0;
    bool extend_end = a_flags >= 2.0;
    if (endpoint < 0.5 && extend_start) {
        base_px -= dir * (a_width_px * 0.5);
    } else if (endpoint >= 0.5 && extend_end) {
        base_px += dir * (a_width_px * 0.5);
    }
    vec2 expanded_px = base_px + side * side_sign * (a_width_px * 0.5);
    vec2 expanded_ndc = expanded_px / viewport * 2.0 - 1.0;

    vec4 clip = mix(c0, c1, endpoint);
    clip.xy = expanded_ndc * clip.w;
    gl_Position = clip;
    v_color = a_color;
}
)";
}

std::string make_screen_line_cap_vert() {
    return std::string("#version 450 core\n") + kScreenLineCommon + R"(
layout(location=0) in vec2 a_local;
layout(location=1) in vec3 a_center;
layout(location=2) in float a_width_px;
layout(location=3) in vec3 a_neighbor;
layout(location=4) in float a_flags;
layout(location=5) in vec4 a_color;

layout(location=0) out vec4 v_color;

void main() {
    vec4 c0 = pc.u_view_projection * vec4(a_center, 1.0);
    vec4 c1 = pc.u_view_projection * vec4(a_neighbor, 1.0);

    vec2 viewport = max(pc.u_viewport.xy, vec2(1.0));
    vec2 ndc0 = c0.xy / c0.w;
    vec2 ndc1 = c1.xy / c1.w;
    vec2 px0 = (ndc0 * 0.5 + 0.5) * viewport;
    vec2 px1 = (ndc1 * 0.5 + 0.5) * viewport;

    vec2 dir = px0 - px1;
    float len = length(dir);
    if (len < 1.0e-5) {
        dir = vec2(1.0, 0.0);
    } else {
        dir /= len;
    }

    vec2 side = vec2(-dir.y, dir.x);
    vec2 expanded_px = px0
        + dir * a_local.x * (a_width_px * 0.5)
        + side * a_local.y * (a_width_px * 0.5);
    vec2 expanded_ndc = expanded_px / viewport * 2.0 - 1.0;

    vec4 clip = c0;
    clip.xy = expanded_ndc * clip.w;
    gl_Position = clip;
    v_color = a_color;
}
)";
}

std::string make_screen_line_join_vert() {
    return std::string("#version 450 core\n") + kScreenLineCommon + R"(
layout(location=0) in float a_corner;
layout(location=1) in vec3 a_prev;
layout(location=2) in float a_width_px;
layout(location=3) in vec3 a_center;
layout(location=4) in float a_flags;
layout(location=5) in vec3 a_next;
layout(location=6) in vec4 a_color;

layout(location=0) out vec4 v_color;

void main() {
    vec4 cp = pc.u_view_projection * vec4(a_prev, 1.0);
    vec4 cc = pc.u_view_projection * vec4(a_center, 1.0);
    vec4 cn = pc.u_view_projection * vec4(a_next, 1.0);

    vec2 viewport = max(pc.u_viewport.xy, vec2(1.0));
    vec2 px_prev = ((cp.xy / cp.w) * 0.5 + 0.5) * viewport;
    vec2 px_center = ((cc.xy / cc.w) * 0.5 + 0.5) * viewport;
    vec2 px_next = ((cn.xy / cn.w) * 0.5 + 0.5) * viewport;

    vec2 d0 = px_center - px_prev;
    vec2 d1 = px_next - px_center;
    float len0 = length(d0);
    float len1 = length(d1);
    if (len0 < 1.0e-5 || len1 < 1.0e-5) {
        gl_Position = cc;
        v_color = a_color;
        return;
    }
    d0 /= len0;
    d1 /= len1;

    float cross_z = d0.x * d1.y - d0.y * d1.x;
    float side_sign = cross_z >= 0.0 ? 1.0 : -1.0;
    if (abs(cross_z) < 1.0e-5) {
        side_sign = 0.0;
    }

    vec2 side0 = vec2(-d0.y, d0.x);
    vec2 side1 = vec2(-d1.y, d1.x);
    vec2 p = px_center;
    if (a_corner > 0.5 && a_corner < 1.5) {
        p = px_center + side0 * side_sign * (a_width_px * 0.5);
    } else if (a_corner >= 1.5) {
        p = px_center + side1 * side_sign * (a_width_px * 0.5);
    }

    vec2 ndc = p / viewport * 2.0 - 1.0;
    vec4 clip = cc;
    clip.xy = ndc * clip.w;
    gl_Position = clip;
    v_color = a_color;
}
)";
}

std::string make_screen_line_frag() {
    return std::string("#version 450 core\n") + R"(
layout(location=0) in vec4 v_color;
layout(location=0) out vec4 frag_color;

void main() {
    frag_color = v_color;
}
)";
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

void ScreenSpaceLineRenderer::ensure_resources(RenderContext2& ctx) {
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

    if (!vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_screen_line_vert();
        desc.debug_name = "tgfx2_screen_space_line_vs";
        vertex_shader_ = device.create_shader(desc);
    }

    if (!fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_screen_line_frag();
        desc.debug_name = "tgfx2_screen_space_line_fs";
        fragment_shader_ = device.create_shader(desc);
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

    if (!cap_vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_screen_line_cap_vert();
        desc.debug_name = "tgfx2_screen_space_line_cap_vs";
        cap_vertex_shader_ = device.create_shader(desc);
    }

    if (!cap_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_screen_line_frag();
        desc.debug_name = "tgfx2_screen_space_line_cap_fs";
        cap_fragment_shader_ = device.create_shader(desc);
    }

    if (!join_vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_screen_line_join_vert();
        desc.debug_name = "tgfx2_screen_space_line_join_vs";
        join_vertex_shader_ = device.create_shader(desc);
    }

    if (!join_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_screen_line_frag();
        desc.debug_name = "tgfx2_screen_space_line_join_fs";
        join_fragment_shader_ = device.create_shader(desc);
    }
}

void ScreenSpaceLineRenderer::ensure_cap_template(RenderContext2& ctx,
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

void ScreenSpaceLineRenderer::draw_polyline(
        RenderContext2& ctx,
        std::span<const LinePoint3> points,
        const ScreenSpaceLineStyle& style,
        const ScreenSpaceLineParams& params) {
    if (points.size() < 2 || style.width_px <= 0.0f) {
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
        instance.width_px = style.width_px;
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
            cap.width_px = style.width_px;
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
            cap.width_px = style.width_px;
            cap.neighbor[0] = prev.x;
            cap.neighbor[1] = prev.y;
            cap.neighbor[2] = prev.z;
            std::memcpy(cap.color, style.color.data(), sizeof(cap.color));
            cap_instances.push_back(cap);
        }
    }

    if (style.join == LineJoinStyle::Bevel) {
        join_instances.reserve(clean_points.size() > 2 ? clean_points.size() - 2 : 0);
        for (size_t i = 1; i + 1 < clean_points.size(); ++i) {
            const LinePoint3 prev = clean_points[i - 1];
            const LinePoint3 center = clean_points[i];
            const LinePoint3 next = clean_points[i + 1];
            JoinInstance join{};
            join.prev[0] = prev.x;
            join.prev[1] = prev.y;
            join.prev[2] = prev.z;
            join.width_px = style.width_px;
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

    const UploadedInstanceStream segment_stream = upload_instance_stream(
        ctx,
        instances.data(),
        instances.size() * sizeof(SegmentInstance));
    if (!segment_stream.buffer) {
        return;
    }

    ScreenLinePush push{};
    std::memcpy(push.view_projection,
                params.view_projection.data(),
                sizeof(push.view_projection));
    push.viewport[0] = std::max(params.viewport_width, 1.0f);
    push.viewport[1] = std::max(params.viewport_height, 1.0f);

    VertexBufferLayout corners;
    corners.stride = sizeof(CornerVertex);
    corners.per_instance = false;
    corners.attributes = {
        {0, VertexFormat::Float2, 0},
    };

    VertexBufferLayout segment;
    segment.stride = sizeof(SegmentInstance);
    segment.per_instance = true;
    segment.attributes = {
        {1, VertexFormat::Float3, offsetof(SegmentInstance, p0)},
        {2, VertexFormat::Float,  offsetof(SegmentInstance, width_px)},
        {3, VertexFormat::Float3, offsetof(SegmentInstance, p1)},
        {4, VertexFormat::Float,  offsetof(SegmentInstance, flags)},
        {5, VertexFormat::Float4, offsetof(SegmentInstance, color)},
    };

    ctx.bind_shader(vertex_shader_, fragment_shader_);
    ctx.set_vertex_layouts({corners, segment});
    ctx.set_topology(PrimitiveTopology::TriangleList);
    ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
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
        cap_corners.attributes = {
            {0, VertexFormat::Float2, 0},
        };

        VertexBufferLayout cap_layout;
        cap_layout.stride = sizeof(CapInstance);
        cap_layout.per_instance = true;
        cap_layout.attributes = {
            {1, VertexFormat::Float3, offsetof(CapInstance, center)},
            {2, VertexFormat::Float,  offsetof(CapInstance, width_px)},
            {3, VertexFormat::Float3, offsetof(CapInstance, neighbor)},
            {4, VertexFormat::Float,  offsetof(CapInstance, flags)},
            {5, VertexFormat::Float4, offsetof(CapInstance, color)},
        };

        ctx.bind_shader(cap_vertex_shader_, cap_fragment_shader_);
        ctx.set_vertex_layouts({cap_corners, cap_layout});
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
        ctx.draw_arrays_instanced(
            cap_corner_vbo_,
            0,
            cap_stream.buffer,
            cap_stream.offset,
            cap_corner_count_,
            static_cast<uint32_t>(cap_instances.size()));
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
        join_corners.attributes = {
            {0, VertexFormat::Float, 0},
        };

        VertexBufferLayout join_layout;
        join_layout.stride = sizeof(JoinInstance);
        join_layout.per_instance = true;
        join_layout.attributes = {
            {1, VertexFormat::Float3, offsetof(JoinInstance, prev)},
            {2, VertexFormat::Float,  offsetof(JoinInstance, width_px)},
            {3, VertexFormat::Float3, offsetof(JoinInstance, center)},
            {4, VertexFormat::Float,  offsetof(JoinInstance, flags)},
            {5, VertexFormat::Float3, offsetof(JoinInstance, next)},
            {6, VertexFormat::Float4, offsetof(JoinInstance, color)},
        };

        ctx.bind_shader(join_vertex_shader_, join_fragment_shader_);
        ctx.set_vertex_layouts({join_corners, join_layout});
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
        ctx.draw_arrays_instanced(
            join_corner_vbo_,
            0,
            join_stream.buffer,
            join_stream.offset,
            static_cast<uint32_t>(kBevelJoinCorners.size()),
            static_cast<uint32_t>(join_instances.size()));
    }
}

void ScreenSpaceLineRenderer::release(RenderContext2& ctx) {
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
    if (vertex_shader_) {
        device.destroy(vertex_shader_);
        vertex_shader_ = {};
    }
    if (fragment_shader_) {
        device.destroy(fragment_shader_);
        fragment_shader_ = {};
    }
    if (cap_vertex_shader_) {
        device.destroy(cap_vertex_shader_);
        cap_vertex_shader_ = {};
    }
    if (cap_fragment_shader_) {
        device.destroy(cap_fragment_shader_);
        cap_fragment_shader_ = {};
    }
    if (join_vertex_shader_) {
        device.destroy(join_vertex_shader_);
        join_vertex_shader_ = {};
    }
    if (join_fragment_shader_) {
        device.destroy(join_fragment_shader_);
        join_fragment_shader_ = {};
    }
}

} // namespace tgfx
