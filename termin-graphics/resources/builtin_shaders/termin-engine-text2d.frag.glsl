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

layout(binding = 4) uniform sampler2D u_font_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

void main() {
    float a = texture(u_font_atlas, v_uv).r * pc.u_color.a;
    if (a < (1.0 / 255.0)) discard;
    frag_color = vec4(pc.u_color.rgb, a);
}
