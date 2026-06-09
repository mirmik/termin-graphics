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

layout(binding = 4) uniform sampler2D u_font_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

void main() {
    float a = texture(u_font_atlas, v_uv).r * pc.u_color.a;
    if (a < (1.0 / 255.0)) discard;
    frag_color = vec4(pc.u_color.rgb, a);
}
