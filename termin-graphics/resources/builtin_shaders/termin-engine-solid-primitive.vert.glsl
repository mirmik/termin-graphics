#version 450 core

struct SolidPushData {
    mat4 u_mvp;
    vec4 u_color;
};

#ifdef VULKAN
layout(push_constant) uniform SolidPushBlock { SolidPushData pc; };
#else
layout(std140, binding = 14) uniform SolidPushBlock { SolidPushData pc; };
#endif

layout(location = 0) in vec3 a_position;
layout(location = 0) out vec4 v_color;

void main() {
    v_color = pc.u_color;
    gl_Position = pc.u_mvp * vec4(a_position, 1.0);
}
