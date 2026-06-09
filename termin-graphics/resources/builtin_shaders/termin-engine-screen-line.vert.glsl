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

layout(location = 0) in vec2 a_corner;
layout(location = 1) in vec3 a_p0;
layout(location = 2) in float a_width_px;
layout(location = 3) in vec3 a_p1;
layout(location = 4) in float a_flags;
layout(location = 5) in vec4 a_color;

layout(location = 0) out vec4 v_color;

void main() {
    vec4 c0 = pc.u_view_projection * vec4(a_p0, 1.0);
    vec4 c1 = pc.u_view_projection * vec4(a_p1, 1.0);

    float endpoint = a_corner.x;
    float side_sign = a_corner.y;

    vec2 viewport = max(pc.u_viewport.xy, vec2(1.0));
    vec2 ndc0 = c0.xy / c0.w;
    vec2 ndc1 = c1.xy / c1.w;
    vec2 px0 = (ndc0 * 0.5 + 0.5) * viewport;
    vec2 px1 = (ndc1 * 0.5 + 0.5) * viewport;

    vec2 dir = px1 - px0;
    float len = length(dir);
    if (len < 1.0e-5) {
        dir = vec2(1.0, 0.0);
    } else {
        dir /= len;
    }

    vec2 side = vec2(-dir.y, dir.x);
    vec2 base_px = mix(px0, px1, endpoint);
    bool extend_start = mod(a_flags, 2.0) >= 1.0;
    bool extend_end = a_flags >= 2.0;
    if (endpoint < 0.5 && extend_start) {
        base_px -= dir * (a_width_px * 0.5);
    } else if (endpoint >= 0.5 && extend_end) {
        base_px += dir * (a_width_px * 0.5);
    }
    vec2 expanded_px = base_px + side * side_sign * (a_width_px * 0.5);
    vec2 expanded_ndc = expanded_px / viewport * 2.0 - 1.0;

    vec4 clip = mix(c0, c1, endpoint);
    clip.xy = expanded_ndc * clip.w;
    gl_Position = clip;
    v_color = a_color;
}
