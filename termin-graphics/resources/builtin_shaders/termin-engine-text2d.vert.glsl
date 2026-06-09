#version 450 core
struct Text2DPushData {
    mat4 u_projection;
    vec4 u_color;
};
#ifdef VULKAN
layout(push_constant) uniform Text2DPushBlock { Text2DPushData pc; };
#else
layout(std140, binding = 14) uniform Text2DPushBlock { Text2DPushData pc; };
#endif

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_uv_pad;

layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = pc.u_projection * vec4(a_pos.xy, 0.0, 1.0);
    v_uv = a_uv_pad.xy;
}
