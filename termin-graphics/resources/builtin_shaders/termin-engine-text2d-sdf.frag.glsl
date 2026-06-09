#version 450 core
struct Text2DSdfPushData {
    mat4 u_projection;
    vec4 u_color;
    float u_smoothing;
};
#ifdef VULKAN
layout(push_constant) uniform Text2DSdfPushBlock { Text2DSdfPushData pc; };
#else
layout(std140, binding = 14) uniform Text2DSdfPushBlock { Text2DSdfPushData pc; };
#endif

layout(binding = 4) uniform sampler2D u_font_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_color;

void main() {
    float d = texture(u_font_atlas, v_uv).r;
    float a = smoothstep(0.5 - pc.u_smoothing, 0.5 + pc.u_smoothing, d)
            * pc.u_color.a;
    if (a < (1.0 / 255.0)) discard;
    frag_color = vec4(pc.u_color.rgb, a);
}
