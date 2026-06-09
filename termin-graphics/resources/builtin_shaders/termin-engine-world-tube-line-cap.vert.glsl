#version 450 core
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

layout(location = 0) in vec3 a_corner;
layout(location = 1) in vec3 a_center;
layout(location = 2) in float a_width;
layout(location = 3) in vec3 a_neighbor;
layout(location = 4) in vec4 a_color;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;

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
