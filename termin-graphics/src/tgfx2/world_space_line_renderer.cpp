#include "tgfx2/world_space_line_renderer.hpp"

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

constexpr const char* kWorldLineCommon = R"(
struct WorldLinePush {
    mat4 u_view_projection;
    vec4 u_camera_position;
};
#ifdef VULKAN
layout(push_constant) uniform WorldLinePushBlock { WorldLinePush pc; };
#else
layout(std140, binding = 14) uniform WorldLinePushBlock { WorldLinePush pc; };
#endif

vec3 safe_normalize(vec3 v, vec3 fallback) {
    float len = length(v);
    if (len < 1.0e-6) {
        return fallback;
    }
    return v / len;
}

vec3 billboard_side(vec3 segment_dir, vec3 point) {
    vec3 to_eye = safe_normalize(pc.u_camera_position.xyz - point, vec3(0.0, 0.0, 1.0));
    vec3 side = cross(segment_dir, to_eye);
    float len = length(side);
    if (len >= 1.0e-6) {
        return side / len;
    }

    side = cross(segment_dir, vec3(0.0, 0.0, 1.0));
    len = length(side);
    if (len >= 1.0e-6) {
        return side / len;
    }

    return safe_normalize(cross(segment_dir, vec3(0.0, 1.0, 0.0)), vec3(1.0, 0.0, 0.0));
}
)";

constexpr const char* kLineLightingCommon = R"(
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

vec3 apply_line_lighting(vec3 base_color, vec3 normal, vec3 world_pos) {
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

std::string make_world_line_vert() {
    return std::string("#version 450 core\n") + kWorldLineCommon + R"(
layout(location=0) in vec2 a_corner;
layout(location=1) in vec3 a_p0;
layout(location=2) in float a_width;
layout(location=3) in vec3 a_p1;
layout(location=4) in float a_flags;
layout(location=5) in vec4 a_color;

layout(location=0) out vec4 v_color;
layout(location=1) out vec3 v_world_pos;
layout(location=2) out vec3 v_normal;

void main() {
    float endpoint = a_corner.x;
    float side_sign = a_corner.y;
    vec3 dir = safe_normalize(a_p1 - a_p0, vec3(1.0, 0.0, 0.0));
    vec3 base = mix(a_p0, a_p1, endpoint);

    bool extend_start = mod(a_flags, 2.0) >= 1.0;
    bool extend_end = a_flags >= 2.0;
    if (endpoint < 0.5 && extend_start) {
        base -= dir * (a_width * 0.5);
    } else if (endpoint >= 0.5 && extend_end) {
        base += dir * (a_width * 0.5);
    }

    vec3 mid = (a_p0 + a_p1) * 0.5;
    vec3 side = billboard_side(dir, mid);
    vec3 expanded = base + side * side_sign * (a_width * 0.5);

    gl_Position = pc.u_view_projection * vec4(expanded, 1.0);
    v_color = a_color;
    v_world_pos = expanded;
    v_normal = safe_normalize(pc.u_camera_position.xyz - mid, vec3(0.0, 0.0, 1.0));
}
)";
}

std::string make_world_line_cap_vert() {
    return std::string("#version 450 core\n") + kWorldLineCommon + R"(
layout(location=0) in vec2 a_local;
layout(location=1) in vec3 a_center;
layout(location=2) in float a_width;
layout(location=3) in vec3 a_neighbor;
layout(location=4) in float a_flags;
layout(location=5) in vec4 a_color;

layout(location=0) out vec4 v_color;
layout(location=1) out vec3 v_world_pos;
layout(location=2) out vec3 v_normal;

void main() {
    vec3 dir = safe_normalize(a_center - a_neighbor, vec3(1.0, 0.0, 0.0));
    vec3 side = billboard_side(dir, a_center);
    vec3 expanded = a_center
        + dir * a_local.x * (a_width * 0.5)
        + side * a_local.y * (a_width * 0.5);

    gl_Position = pc.u_view_projection * vec4(expanded, 1.0);
    v_color = a_color;
    v_world_pos = expanded;
    v_normal = safe_normalize(pc.u_camera_position.xyz - a_center, vec3(0.0, 0.0, 1.0));
}
)";
}

std::string make_world_line_join_vert() {
    return std::string("#version 450 core\n") + kWorldLineCommon + R"(
layout(location=0) in float a_corner;
layout(location=1) in vec3 a_prev;
layout(location=2) in float a_width;
layout(location=3) in vec3 a_center;
layout(location=4) in float a_flags;
layout(location=5) in vec3 a_next;
layout(location=6) in vec4 a_color;

layout(location=0) out vec4 v_color;
layout(location=1) out vec3 v_world_pos;
layout(location=2) out vec3 v_normal;

void main() {
    vec3 d0 = safe_normalize(a_center - a_prev, vec3(1.0, 0.0, 0.0));
    vec3 d1 = safe_normalize(a_next - a_center, vec3(1.0, 0.0, 0.0));
    vec3 to_eye = safe_normalize(pc.u_camera_position.xyz - a_center, vec3(0.0, 0.0, 1.0));
    float side_sign = dot(cross(d0, d1), to_eye) >= 0.0 ? 1.0 : -1.0;
    if (length(cross(d0, d1)) < 1.0e-6) {
        side_sign = 0.0;
    }

    vec3 side0 = billboard_side(d0, a_center);
    vec3 side1 = billboard_side(d1, a_center);
    vec3 p = a_center;
    if (a_corner > 0.5 && a_corner < 1.5) {
        p = a_center + side0 * side_sign * (a_width * 0.5);
    } else if (a_corner >= 1.5) {
        p = a_center + side1 * side_sign * (a_width * 0.5);
    }

    gl_Position = pc.u_view_projection * vec4(p, 1.0);
    v_color = a_color;
    v_world_pos = p;
    v_normal = to_eye;
}
)";
}

std::string make_world_line_round_join_vert() {
    return std::string("#version 450 core\n") + kWorldLineCommon + R"(
layout(location=0) in vec2 a_local;
layout(location=1) in vec3 a_center;
layout(location=2) in float a_width;
layout(location=3) in vec3 a_neighbor;
layout(location=4) in float a_flags;
layout(location=5) in vec4 a_color;

layout(location=0) out vec4 v_color;
layout(location=1) out vec3 v_world_pos;
layout(location=2) out vec3 v_normal;

void main() {
    vec3 dir = safe_normalize(a_neighbor - a_center, vec3(1.0, 0.0, 0.0));
    vec3 to_eye = safe_normalize(pc.u_camera_position.xyz - a_center, vec3(0.0, 0.0, 1.0));
    vec3 axis0 = billboard_side(dir, a_center);
    vec3 axis1 = safe_normalize(cross(to_eye, axis0), dir);
    vec3 expanded = a_center
        + axis0 * a_local.x * (a_width * 0.5)
        + axis1 * a_local.y * (a_width * 0.5);

    gl_Position = pc.u_view_projection * vec4(expanded, 1.0);
    v_color = a_color;
    v_world_pos = expanded;
    v_normal = to_eye;
}
)";
}

std::string make_world_line_frag() {
    return std::string("#version 450 core\n") + R"(
layout(location=0) in vec4 v_color;
layout(location=0) out vec4 frag_color;

void main() {
    frag_color = v_color;
}
)";
}

std::string make_world_line_lit_frag() {
    return std::string("#version 450 core\n") + kLineLightingCommon + R"(
layout(location=0) in vec4 v_color;
layout(location=1) in vec3 v_world_pos;
layout(location=2) in vec3 v_normal;
layout(location=0) out vec4 frag_color;

void main() {
    frag_color = vec4(apply_line_lighting(v_color.rgb, v_normal, v_world_pos), v_color.a);
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

    if (!vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_world_line_vert();
        desc.debug_name = "tgfx2_world_space_line_vs";
        vertex_shader_ = device.create_shader(desc);
    }

    if (!fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_world_line_frag();
        desc.debug_name = "tgfx2_world_space_line_fs";
        fragment_shader_ = device.create_shader(desc);
    }

    if (!cap_vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_world_line_cap_vert();
        desc.debug_name = "tgfx2_world_space_line_cap_vs";
        cap_vertex_shader_ = device.create_shader(desc);
    }

    if (!cap_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_world_line_frag();
        desc.debug_name = "tgfx2_world_space_line_cap_fs";
        cap_fragment_shader_ = device.create_shader(desc);
    }

    if (!join_vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_world_line_join_vert();
        desc.debug_name = "tgfx2_world_space_line_join_vs";
        join_vertex_shader_ = device.create_shader(desc);
    }

    if (!join_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_world_line_frag();
        desc.debug_name = "tgfx2_world_space_line_join_fs";
        join_fragment_shader_ = device.create_shader(desc);
    }

    if (!round_join_vertex_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Vertex;
        desc.source = make_world_line_round_join_vert();
        desc.debug_name = "tgfx2_world_space_line_round_join_vs";
        round_join_vertex_shader_ = device.create_shader(desc);
    }

    if (!round_join_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_world_line_frag();
        desc.debug_name = "tgfx2_world_space_line_round_join_fs";
        round_join_fragment_shader_ = device.create_shader(desc);
    }

    if (!lit_fragment_shader_) {
        ShaderDesc desc;
        desc.stage = ShaderStage::Fragment;
        desc.source = make_world_line_lit_frag();
        desc.debug_name = "tgfx2_world_space_line_lit_fs";
        lit_fragment_shader_ = device.create_shader(desc);
    }
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
    corners.attributes = {
        {0, VertexFormat::Float2, 0},
    };

    VertexBufferLayout segment;
    segment.stride = sizeof(SegmentInstance);
    segment.per_instance = true;
    segment.attributes = {
        {1, VertexFormat::Float3, offsetof(SegmentInstance, p0)},
        {2, VertexFormat::Float,  offsetof(SegmentInstance, width)},
        {3, VertexFormat::Float3, offsetof(SegmentInstance, p1)},
        {4, VertexFormat::Float,  offsetof(SegmentInstance, flags)},
        {5, VertexFormat::Float4, offsetof(SegmentInstance, color)},
    };

    ShaderHandle fragment_shader = params.lighting_enabled ? lit_fragment_shader_ : fragment_shader_;

    ctx.bind_shader(vertex_shader_, fragment_shader);
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
            {2, VertexFormat::Float,  offsetof(CapInstance, width)},
            {3, VertexFormat::Float3, offsetof(CapInstance, neighbor)},
            {4, VertexFormat::Float,  offsetof(CapInstance, flags)},
            {5, VertexFormat::Float4, offsetof(CapInstance, color)},
        };

        ctx.bind_shader(cap_vertex_shader_, params.lighting_enabled ? lit_fragment_shader_ : cap_fragment_shader_);
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
        round_join_corners.attributes = {
            {0, VertexFormat::Float2, 0},
        };

        VertexBufferLayout round_join_layout;
        round_join_layout.stride = sizeof(CapInstance);
        round_join_layout.per_instance = true;
        round_join_layout.attributes = {
            {1, VertexFormat::Float3, offsetof(CapInstance, center)},
            {2, VertexFormat::Float,  offsetof(CapInstance, width)},
            {3, VertexFormat::Float3, offsetof(CapInstance, neighbor)},
            {4, VertexFormat::Float,  offsetof(CapInstance, flags)},
            {5, VertexFormat::Float4, offsetof(CapInstance, color)},
        };

        ctx.bind_shader(round_join_vertex_shader_, params.lighting_enabled ? lit_fragment_shader_ : round_join_fragment_shader_);
        ctx.set_vertex_layouts({round_join_corners, round_join_layout});
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.set_push_constants(&push, static_cast<uint32_t>(sizeof(push)));
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
        join_corners.attributes = {
            {0, VertexFormat::Float, 0},
        };

        VertexBufferLayout join_layout;
        join_layout.stride = sizeof(JoinInstance);
        join_layout.per_instance = true;
        join_layout.attributes = {
            {1, VertexFormat::Float3, offsetof(JoinInstance, prev)},
            {2, VertexFormat::Float,  offsetof(JoinInstance, width)},
            {3, VertexFormat::Float3, offsetof(JoinInstance, center)},
            {4, VertexFormat::Float,  offsetof(JoinInstance, flags)},
            {5, VertexFormat::Float3, offsetof(JoinInstance, next)},
            {6, VertexFormat::Float4, offsetof(JoinInstance, color)},
        };

        ctx.bind_shader(join_vertex_shader_, params.lighting_enabled ? lit_fragment_shader_ : join_fragment_shader_);
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
    if (round_join_vertex_shader_) {
        device.destroy(round_join_vertex_shader_);
        round_join_vertex_shader_ = {};
    }
    if (round_join_fragment_shader_) {
        device.destroy(round_join_fragment_shader_);
        round_join_fragment_shader_ = {};
    }
    if (lit_fragment_shader_) {
        device.destroy(lit_fragment_shader_);
        lit_fragment_shader_ = {};
    }
}

} // namespace tgfx
