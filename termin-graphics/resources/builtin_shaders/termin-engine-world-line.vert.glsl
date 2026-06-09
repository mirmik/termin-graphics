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

layout(location = 0) in vec2 a_corner;
layout(location = 1) in vec3 a_p0;
layout(location = 2) in float a_width;
layout(location = 3) in vec3 a_p1;
layout(location = 4) in float a_flags;
layout(location = 5) in vec4 a_color;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;

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
    v_world_pos = expanded;
    v_normal = safe_normalize(pc.u_camera_position.xyz - mid, vec3(0.0, 0.0, 1.0));
    v_uv = vec2(endpoint, side_sign * 0.5 + 0.5);
    v_color = a_color;
}
