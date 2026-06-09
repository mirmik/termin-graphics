#version 450 core
struct ImmediatePushData {
    mat4 u_vp;
};
#ifdef VULKAN
layout(push_constant) uniform ImmediatePushBlock { ImmediatePushData pc; };
#else
layout(std140, binding = 14) uniform ImmediatePushBlock { ImmediatePushData pc; };
#endif

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 0) out vec4 v_color;

void main() {
    v_color = a_color;
    gl_Position = pc.u_vp * vec4(a_position, 1.0);
}
