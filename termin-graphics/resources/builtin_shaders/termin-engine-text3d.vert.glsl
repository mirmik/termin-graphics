#version 450 core
struct Text3DPush {
    mat4 u_mvp;
    vec4 u_color;
    vec4 u_cam_right;
    vec4 u_cam_up;
};
#ifdef VULKAN
layout(push_constant) uniform Text3DPushBlock { Text3DPush pc; };
#else
layout(std140, binding = 14) uniform Text3DPushBlock { Text3DPush pc; };
#endif

layout(location = 0) in vec3 a_world_pos;
layout(location = 1) in vec4 a_offset_uv;

layout(location = 0) out vec2 v_uv;

void main() {
    vec3 pos = a_world_pos
             + pc.u_cam_right.xyz * a_offset_uv.x
             + pc.u_cam_up.xyz    * a_offset_uv.y;
    gl_Position = pc.u_mvp * vec4(pos, 1.0);
    v_uv = a_offset_uv.zw;
}
