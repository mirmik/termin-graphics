#version 450 core
struct IdPushData {
    mat4 u_model;
    vec4 u_pickColor;
};
#ifdef VULKAN
layout(push_constant) uniform IdPushBlock { IdPushData pc; };
#else
layout(std140, binding = 14) uniform IdPushBlock { IdPushData pc; };
#endif

layout(location=0) out vec4 fragColor;

void main() {
    fragColor = vec4(pc.u_pickColor.rgb, 1.0);
}
