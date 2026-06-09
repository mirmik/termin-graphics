#version 450 core
layout(location = 0) in vec2 v_uv;
layout(binding = 4) uniform sampler2D u_tex;
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = texture(u_tex, v_uv);
}
