#version 450 core
struct HighlightPushData {
    vec3  u_selected_color;
    float u_enabled;
    vec3  u_outline_color;
    float _pad0;
    vec2  u_texel_size;
};
#ifdef VULKAN
layout(push_constant) uniform HighlightPushBlock { HighlightPushData pc; };
#else
layout(std140, binding = 14) uniform HighlightPushBlock { HighlightPushData pc; };
#endif

layout(location = 0) in vec2 v_uv;
layout(binding = 4) uniform sampler2D u_color;
layout(binding = 5) uniform sampler2D u_id;
layout(location = 0) out vec4 FragColor;

float is_selected(vec3 id_color) {
    float d = distance(id_color, pc.u_selected_color);
    return float(d < 0.001);
}

void main() {
    vec4 base = texture(u_color, v_uv);

    if (pc.u_enabled < 0.5) {
        FragColor = base;
        return;
    }

    vec3 id_center = texture(u_id, v_uv).rgb;
    float center_sel = is_selected(id_center);

    vec2 ts = pc.u_texel_size;
    float neigh_sel = 0.0;
    neigh_sel = max(neigh_sel, is_selected(texture(u_id, v_uv + vec2( ts.x,  0.0)).rgb));
    neigh_sel = max(neigh_sel, is_selected(texture(u_id, v_uv + vec2(-ts.x,  0.0)).rgb));
    neigh_sel = max(neigh_sel, is_selected(texture(u_id, v_uv + vec2( 0.0,  ts.y)).rgb));
    neigh_sel = max(neigh_sel, is_selected(texture(u_id, v_uv + vec2( 0.0, -ts.y)).rgb));

    float outline = 0.0;
    outline = max(outline, center_sel * (1.0 - neigh_sel));
    outline = max(outline, neigh_sel * (1.0 - center_sel));

    if (outline > 0.0) {
        float k = 0.8;
        vec3 col = mix(base.rgb, pc.u_outline_color, k);
        FragColor = vec4(col, base.a);
    } else {
        FragColor = base;
    }
}
