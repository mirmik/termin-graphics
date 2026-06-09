#version 450 core
struct ScreenLinePush {
    mat4 u_view_projection;
    vec4 u_viewport;
};
#ifdef VULKAN
layout(push_constant) uniform ScreenLinePushBlock { ScreenLinePush pc; };
#else
layout(std140, binding = 14) uniform ScreenLinePushBlock { ScreenLinePush pc; };
#endif

layout(location = 0) in vec2 a_local;
layout(location = 1) in vec3 a_center;
layout(location = 2) in float a_width_px;
layout(location = 3) in vec3 a_neighbor;
layout(location = 4) in float a_flags;
layout(location = 5) in vec4 a_color;

layout(location = 0) out vec4 v_color;

void main() {
    vec4 cc = pc.u_view_projection * vec4(a_center, 1.0);

    vec2 viewport = max(pc.u_viewport.xy, vec2(1.0));
    vec2 px_center = ((cc.xy / cc.w) * 0.5 + 0.5) * viewport;
    vec2 expanded_px = px_center + a_local * (a_width_px * 0.5);
    vec2 expanded_ndc = expanded_px / viewport * 2.0 - 1.0;

    vec4 clip = cc;
    clip.xy = expanded_ndc * clip.w;
    gl_Position = clip;
    v_color = a_color;
}
