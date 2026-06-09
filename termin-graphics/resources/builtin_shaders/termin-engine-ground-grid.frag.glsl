#version 450 core

layout(std140, binding = 0) uniform GridParams {
    mat4 u_inv_vp;
    mat4 u_view;
    mat4 u_projection;
    float u_near;
    float u_far;
};

layout(location = 0) in vec3 v_near_point;
layout(location = 1) in vec3 v_far_point;
layout(location = 0) out vec4 fragColor;

vec4 grid(vec3 pos, float scale, vec4 color) {
    vec2 coord = pos.xy / scale;
    vec2 d = fwidth(coord);
    vec2 grid_line = abs(fract(coord - 0.5) - 0.5) / d;
    float line = min(grid_line.x, grid_line.y);
    float alpha = 1.0 - min(line, 1.0);
    return vec4(color.rgb, color.a * alpha);
}

float compute_depth(vec3 pos) {
    vec4 clip = u_projection * u_view * vec4(pos, 1.0);
    return (clip.z / clip.w) * 0.5 + 0.5;
}

float compute_fade(vec3 pos) {
    vec4 clip = u_projection * u_view * vec4(pos, 1.0);
    float ndc_depth = clip.z / clip.w;
    float linear_depth =
        (2.0 * u_near * u_far) /
        (u_far + u_near - ndc_depth * (u_far - u_near));
    return max(0.0, 1.0 - linear_depth / u_far);
}

void main() {
    vec3 ray = v_far_point - v_near_point;
    float t = -v_near_point.z / ray.z;
    if (t < 0.0) {
        discard;
    }

    vec3 world_pos = v_near_point + t * ray;

    vec4 small_grid = grid(world_pos, 1.0, vec4(0.5, 0.5, 0.5, 0.3));
    vec4 large_grid = grid(world_pos, 10.0, vec4(0.5, 0.5, 0.5, 0.5));

    vec2 dxy = fwidth(vec2(world_pos.y, world_pos.x));
    float x_axis = 1.0 - min(abs(world_pos.y) / dxy.x, 1.0);
    float y_axis = 1.0 - min(abs(world_pos.x) / dxy.y, 1.0);

    vec4 color = small_grid + large_grid * (1.0 - small_grid.a);
    color.rgb = mix(color.rgb, vec3(0.8, 0.2, 0.2), x_axis * 0.8);
    color.rgb = mix(color.rgb, vec3(0.2, 0.8, 0.2), y_axis * 0.8);
    color.a = max(color.a, max(x_axis * 0.8, y_axis * 0.8));

    float fade = compute_fade(world_pos);
    color.a *= fade;

    gl_FragDepth = compute_depth(world_pos);
    fragColor = color;
}
