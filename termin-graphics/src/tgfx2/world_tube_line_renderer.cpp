#include "tgfx2/world_tube_line_renderer.hpp"

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

constexpr const char* kTubeLightingCommon = R"(
const int LIGHT_TYPE_DIRECTIONAL = 0;
const int LIGHT_TYPE_POINT = 1;
const int LIGHT_TYPE_SPOT = 2;
const int MAX_LIGHTS = 8;

struct LightData {
    vec4 color_intensity;
    vec4 direction_range;
    vec4 position_type;
    vec4 attenuation_inner;
    vec4 outer_cascade;
};

layout(std140, binding = 0) uniform LightingBlock {
    LightData u_lights[MAX_LIGHTS];
    vec4 u_ambient_data;
    vec4 u_camera_light_count;
    vec4 u_shadow_settings;
};

float distance_attenuation(vec3 attenuation, float range, float dist) {
    float denom = attenuation.x + attenuation.y * dist + attenuation.z * dist * dist;
    float value = denom > 0.0 ? 1.0 / denom : 1.0;
    if (range > 0.0 && dist > range) {
        value = 0.0;
    }
    return value;
}

float spot_weight(vec3 light_dir, vec3 L, float inner_angle, float outer_angle) {
    float cos_theta = dot(light_dir, -L);
    float cos_outer = cos(outer_angle);
    float cos_inner = cos(inner_angle);
    if (cos_theta <= cos_outer) {
        return 0.0;
    }
    if (cos_theta >= cos_inner) {
        return 1.0;
    }
    float t = (cos_theta - cos_outer) / max(cos_inner - cos_outer, 1.0e-4);
    return t * t * (3.0 - 2.0 * t);
}

vec3 apply_tube_lighting(vec3 base_color, vec3 normal, vec3 world_pos) {
    vec3 N = normalize(normal);
    vec3 result = base_color * u_ambient_data.rgb * u_ambient_data.w;
    int count = min(int(u_camera_light_count.w), MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        int type = int(u_lights[i].position_type.w);
        vec3 L;
        float weight = 1.0;
        if (type == LIGHT_TYPE_DIRECTIONAL) {
            L = normalize(-u_lights[i].direction_range.xyz);
        } else {
            vec3 to_light = u_lights[i].position_type.xyz - world_pos;
            float dist = length(to_light);
            L = dist > 1.0e-4 ? to_light / dist : vec3(0.0, 1.0, 0.0);
            weight *= distance_attenuation(
                u_lights[i].attenuation_inner.xyz,
                u_lights[i].direction_range.w,
                dist);
            if (type == LIGHT_TYPE_SPOT) {
                weight *= spot_weight(
                    u_lights[i].direction_range.xyz,
                    L,
                    u_lights[i].attenuation_inner.w,
                    u_lights[i].outer_cascade.x);
            }
        }
        float ndotl = abs(dot(N, L));
        result += base_color * u_lights[i].color_intensity.rgb * u_lights[i].color_intensity.w * ndotl * weight;
    }
    return result;
}
)";

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
        vertices.push_back({1.0f, a[0], a[1]});
        vertices.push_back({1.0f, b[0], b[1]});
        vertices.push_back({0.0f, a[0], a[1]});
        vertices.push_back({1.0f, b[0], b[1]});
        vertices.push_back({0.0f, b[0], b[1]});
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
        vertices.push_back({0.0f, a[0], a[1]});
        vertices.push_back({0.0f, b[0], b[1]});
    }
    return vertices;
}

std::string make_tube_common() {
    return R"(
struct TubePush {
    mat4 u_view_projection;
    vec4 u_up_hint;
};
#ifdef VULKAN
layout(push_constant) uniform TubePushBlock { TubePush pc; };
#else
layout(std140, binding = 14) uniform TubePushBlock { TubePush pc; };
#endif

vec3 safe_normalize(vec3 v, vec3 fallback) {
    float len = length(v);
    if (len < 1.0e-6) {
        return fallback;
    }
    return v / len;
}

vec3 basis_axis0(vec3 dir) {
    vec3 up = safe_normalize(pc.u_up_hint.xyz, vec3(0.0, 1.0, 0.0));
    vec3 axis0 = cross(up, dir);
    float len = length(axis0);
    if (len >= 1.0e-6) {
        return axis0 / len;
    }

    axis0 = cross(vec3(1.0, 0.0, 0.0), dir);
    len = length(axis0);
    if (len >= 1.0e-6) {
        return axis0 / len;
    }

    return safe_normalize(cross(vec3(0.0, 0.0, 1.0), dir), vec3(1.0, 0.0, 0.0));
}

vec3 tube_offset(vec3 dir, float axis0_factor, float axis1_factor, float radius) {
    vec3 axis0 = basis_axis0(dir);
    vec3 axis1 = safe_normalize(cross(dir, axis0), vec3(0.0, 1.0, 0.0));
    return (axis0 * axis0_factor + axis1 * axis1_factor) * radius;
}

vec3 tube_normal(vec3 dir, float axis0_factor, float axis1_factor) {
    return safe_normalize(tube_offset(dir, axis0_factor, axis1_factor, 1.0), vec3(0.0, 1.0, 0.0));
}
)";
}

std::string make_tube_body_vert() {
    return std::string("#version 450 core\n") + make_tube_common() + R"(
layout(location=0) in vec3 a_corner;
layout(location=1) in vec3 a_p0;
layout(location=2) in float a_width;
layout(location=3) in vec3 a_p1;
layout(location=4) in vec4 a_color;

layout(location=0) out vec3 v_world_pos;
layout(location=1) out vec3 v_normal;
layout(location=2) out vec2 v_uv;
layout(location=3) out vec4 v_color;

void main() {
    vec3 dir = safe_normalize(a_p1 - a_p0, vec3(1.0, 0.0, 0.0));
    vec3 base = mix(a_p0, a_p1, a_corner.x);
    vec3 expanded = base + tube_offset(dir, a_corner.y, a_corner.z, a_width * 0.5);
    gl_Position = pc.u_view_projection * vec4(expanded, 1.0);
    v_world_pos = expanded;
    v_normal = tube_normal(dir, a_corner.y, a_corner.z);
    v_uv = vec2(a_corner.x, atan(a_corner.z, a_corner.y) / 6.28318530718 + 0.5);
    v_color = a_color;
}
)";
}

std::string make_tube_cap_vert() {
    return std::string("#version 450 core\n") + make_tube_common() + R"(
layout(location=0) in vec3 a_corner;
layout(location=1) in vec3 a_center;
layout(location=2) in float a_width;
layout(location=3) in vec3 a_neighbor;
layout(location=4) in vec4 a_color;

layout(location=0) out vec3 v_world_pos;
layout(location=1) out vec3 v_normal;
layout(location=2) out vec2 v_uv;
layout(location=3) out vec4 v_color;

void main() {
    vec3 dir = safe_normalize(a_neighbor - a_center, vec3(1.0, 0.0, 0.0));
    vec3 expanded = a_center;
    if (a_corner.x < 0.5) {
        expanded += tube_offset(dir, a_corner.y, a_corner.z, a_width * 0.5);
    }
    gl_Position = pc.u_view_projection * vec4(expanded, 1.0);
    v_world_pos = expanded;
    v_normal = a_corner.x < 0.5 ? tube_normal(dir, a_corner.y, a_corner.z) : -dir;
    v_uv = a_corner.x < 0.5
        ? vec2(a_corner.y * 0.5 + 0.5, a_corner.z * 0.5 + 0.5)
        : vec2(0.5, 0.5);
    v_color = a_color;
}
)";
}

std::string make_tube_frag() {
    return std::string("#version 450 core\n") + R"(
layout(location=3) in vec4 v_color;
layout(location=0) out vec4 frag_color;

void main() {
    frag_color = v_color;
}
)";
}

std::string make_tube_lit_frag() {
    return std::string("#version 450 core\n") + kTubeLightingCommon + R"(
layout(location=0) in vec3 v_world_pos;
layout(location=1) in vec3 v_normal;
layout(location=3) in vec4 v_color;
layout(location=0) out vec4 frag_color;

void main() {
    frag_color = vec4(apply_tube_lighting(v_color.rgb, v_normal, v_world_pos), v_color.a);
}
)";
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

    if (!body_vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_tube_body_vert();
        desc.debug_name = "tgfx2_world_tube_line_vs";
        body_vertex_shader_ = device.create_shader(desc);
    }

    if (!body_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_tube_frag();
        desc.debug_name = "tgfx2_world_tube_line_fs";
        body_fragment_shader_ = device.create_shader(desc);
    }

    if (!cap_vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_tube_cap_vert();
        desc.debug_name = "tgfx2_world_tube_line_cap_vs";
        cap_vertex_shader_ = device.create_shader(desc);
    }

    if (!cap_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_tube_frag();
        desc.debug_name = "tgfx2_world_tube_line_cap_fs";
        cap_fragment_shader_ = device.create_shader(desc);
    }

    if (!lit_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_tube_lit_frag();
        desc.debug_name = "tgfx2_world_tube_line_lit_fs";
        lit_fragment_shader_ = device.create_shader(desc);
    }
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

    ctx.bind_shader(body_vertex_shader_, params.fragment_shader
        ? params.fragment_shader
        : (params.lighting_enabled ? lit_fragment_shader_ : body_fragment_shader_));
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

    ctx.bind_shader(cap_vertex_shader_, params.fragment_shader
        ? params.fragment_shader
        : (params.lighting_enabled ? lit_fragment_shader_ : cap_fragment_shader_));
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
    if (body_vertex_shader_) {
        device.destroy(body_vertex_shader_);
        body_vertex_shader_ = {};
    }
    if (body_fragment_shader_) {
        device.destroy(body_fragment_shader_);
        body_fragment_shader_ = {};
    }
    if (cap_vertex_shader_) {
        device.destroy(cap_vertex_shader_);
        cap_vertex_shader_ = {};
    }
    if (cap_fragment_shader_) {
        device.destroy(cap_fragment_shader_);
        cap_fragment_shader_ = {};
    }
    if (lit_fragment_shader_) {
        device.destroy(lit_fragment_shader_);
        lit_fragment_shader_ = {};
    }
    template_sides_ = 0;
}

} // namespace tgfx
