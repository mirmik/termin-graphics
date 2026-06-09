#version 450 core
layout(location = 0) in vec3 a_position;

struct GizmoPushData {
    mat4 u_mvp;
    vec4 u_color;
};
#ifdef VULKAN
layout(push_constant) uniform GizmoPushBlock { GizmoPushData pc; };
#else
layout(std140, binding = 14) uniform GizmoPushBlock { GizmoPushData pc; };
#endif

void main() {
    gl_Position = pc.u_mvp * vec4(a_position, 1.0);
}
