#version 450 core
layout(location = 0) in vec3 a_position;

layout(std140, binding = 0) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
};

struct ShadowPushData {
    mat4 u_model;
};
#ifdef VULKAN
layout(push_constant) uniform ShadowPushBlock { ShadowPushData pc; };
#else
layout(std140, binding = 14) uniform ShadowPushBlock { ShadowPushData pc; };
#endif

void main() {
    vec4 world_pos = pc.u_model * vec4(a_position, 1.0);
    gl_Position = u_projection * u_view * world_pos;
}
