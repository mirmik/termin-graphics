#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

layout(std140, binding = 0) uniform PerFrame {
    mat4  u_view;
    mat4  u_projection;
    float u_near;
    float u_far;
};

struct DepthPushData {
    mat4 u_model;
};
#ifdef VULKAN
layout(push_constant) uniform DepthPushBlock { DepthPushData pc; };
#else
layout(std140, binding = 14) uniform DepthPushBlock { DepthPushData pc; };
#endif

void main() {
    vec4 world_pos = pc.u_model * vec4(a_position, 1.0);
    vec4 view_pos  = u_view * world_pos;
    gl_Position = u_projection * view_pos;
}
