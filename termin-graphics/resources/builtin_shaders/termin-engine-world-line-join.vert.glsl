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

layout(location = 0) in float a_corner;
layout(location = 1) in vec3 a_prev;
layout(location = 2) in float a_width;
layout(location = 3) in vec3 a_center;
layout(location = 4) in float a_flags;
layout(location = 5) in vec3 a_next;
layout(location = 6) in vec4 a_color;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;

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
    v_world_pos = p;
    v_normal = to_eye;
    v_uv = vec2(a_corner * 0.5, side_sign * 0.5 + 0.5);
    v_color = a_color;
}
