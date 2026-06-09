#version 450 core
struct CanvasPushData {
    mat4 u_projection;
    vec4 u_color;
};
#ifdef VULKAN
layout(push_constant) uniform CanvasPushBlock { CanvasPushData pc; };
#else
layout(std140, binding = 14) uniform CanvasPushBlock { CanvasPushData pc; };
#endif

layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = pc.u_color;
}
