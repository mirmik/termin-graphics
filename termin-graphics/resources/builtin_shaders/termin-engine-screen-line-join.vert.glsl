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

layout(location = 0) in float a_corner;
layout(location = 1) in vec3 a_prev;
layout(location = 2) in float a_width_px;
layout(location = 3) in vec3 a_center;
layout(location = 4) in float a_flags;
layout(location = 5) in vec3 a_next;
layout(location = 6) in vec4 a_color;

layout(location = 0) out vec4 v_color;

void main() {
    vec4 cp = pc.u_view_projection * vec4(a_prev, 1.0);
    vec4 cc = pc.u_view_projection * vec4(a_center, 1.0);
    vec4 cn = pc.u_view_projection * vec4(a_next, 1.0);

    vec2 viewport = max(pc.u_viewport.xy, vec2(1.0));
    vec2 px_prev = ((cp.xy / cp.w) * 0.5 + 0.5) * viewport;
    vec2 px_center = ((cc.xy / cc.w) * 0.5 + 0.5) * viewport;
    vec2 px_next = ((cn.xy / cn.w) * 0.5 + 0.5) * viewport;

    vec2 d0 = px_center - px_prev;
    vec2 d1 = px_next - px_center;
    float len0 = length(d0);
    float len1 = length(d1);
    if (len0 < 1.0e-5 || len1 < 1.0e-5) {
        gl_Position = cc;
        v_color = a_color;
        return;
    }
    d0 /= len0;
    d1 /= len1;

    float cross_z = d0.x * d1.y - d0.y * d1.x;
    float side_sign = cross_z >= 0.0 ? 1.0 : -1.0;
    if (abs(cross_z) < 1.0e-5) {
        side_sign = 0.0;
    }

    vec2 side0 = vec2(-d0.y, d0.x);
    vec2 side1 = vec2(-d1.y, d1.x);
    vec2 p = px_center;
    if (a_corner > 0.5 && a_corner < 1.5) {
        p = px_center + side0 * side_sign * (a_width_px * 0.5);
    } else if (a_corner >= 1.5) {
        p = px_center + side1 * side_sign * (a_width_px * 0.5);
    }

    vec2 ndc = p / viewport * 2.0 - 1.0;
    vec4 clip = cc;
    clip.xy = ndc * clip.w;
    gl_Position = clip;
    v_color = a_color;
}
