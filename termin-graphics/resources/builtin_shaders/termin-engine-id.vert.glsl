#version 450 core
layout(location=0) in vec3 a_position;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec2 a_texcoord;

layout(std140, binding = 0) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
};

struct IdPushData {
    mat4 u_model;
    vec4 u_pickColor;
};
#ifdef VULKAN
layout(push_constant) uniform IdPushBlock { IdPushData pc; };
#else
layout(std140, binding = 14) uniform IdPushBlock { IdPushData pc; };
#endif

void main() {
    gl_Position = u_projection * u_view * pc.u_model * vec4(a_position, 1.0);
}
