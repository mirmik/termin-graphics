#version 450 core
struct GizmoPushData {
    mat4 u_mvp;
    vec4 u_color;
};
#ifdef VULKAN
layout(push_constant) uniform GizmoPushBlock { GizmoPushData pc; };
#else
layout(std140, binding = 14) uniform GizmoPushBlock { GizmoPushData pc; };
#endif
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(0.0, 0.0, 0.0, pc.u_color.a);
}
