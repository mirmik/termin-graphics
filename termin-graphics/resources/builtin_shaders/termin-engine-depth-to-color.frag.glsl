#version 450 core

layout(location = 0) in vec2 v_uv;
layout(binding = 9) uniform sampler2D u_depth_tex;
layout(location = 0) out vec4 FragColor;

void main() {
    float d = texture(u_depth_tex, v_uv).r;
    FragColor = vec4(vec3(d), 1.0);
}
