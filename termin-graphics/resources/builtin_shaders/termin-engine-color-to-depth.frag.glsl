#version 450 core

layout(location = 0) in vec2 v_uv;
layout(binding = 9) uniform sampler2D u_color_tex;

void main() {
    float d = texture(u_color_tex, v_uv).r;
    gl_FragDepth = clamp(d, 0.0, 1.0);
}
