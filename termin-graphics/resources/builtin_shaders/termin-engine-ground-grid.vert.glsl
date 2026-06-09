#version 450 core

layout(std140, binding = 0) uniform GridParams {
    mat4 u_inv_vp;
    mat4 u_view;
    mat4 u_projection;
    float u_near;
    float u_far;
};

layout(location = 0) in vec2 a_pos;
layout(location = 0) out vec3 v_near_point;
layout(location = 1) out vec3 v_far_point;

vec3 unproject(vec2 xy, float z) {
    vec4 p = u_inv_vp * vec4(xy, z, 1.0);
    return p.xyz / p.w;
}

void main() {
    v_near_point = unproject(a_pos, 0.0);
    v_far_point = unproject(a_pos, 1.0);
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
