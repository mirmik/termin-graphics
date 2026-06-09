#version 450 core
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

layout(location = 0) in vec2 a_local;
layout(location = 1) in vec3 a_center;
layout(location = 2) in float a_width;
layout(location = 3) in vec3 a_neighbor;
layout(location = 4) in float a_flags;
layout(location = 5) in vec4 a_color;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;

void main() {
    vec3 dir = safe_normalize(a_neighbor - a_center, vec3(1.0, 0.0, 0.0));
    vec3 to_eye = safe_normalize(pc.u_camera_position.xyz - a_center, vec3(0.0, 0.0, 1.0));
    vec3 axis0 = billboard_side(dir, a_center);
    vec3 axis1 = safe_normalize(cross(to_eye, axis0), dir);
    vec3 expanded = a_center
        + axis0 * a_local.x * (a_width * 0.5)
        + axis1 * a_local.y * (a_width * 0.5);

    gl_Position = pc.u_view_projection * vec4(expanded, 1.0);
    v_world_pos = expanded;
    v_normal = to_eye;
    v_uv = a_local * 0.5 + 0.5;
    v_color = a_color;
}
