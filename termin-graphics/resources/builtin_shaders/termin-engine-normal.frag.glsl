#version 450 core

layout(location = 0) in vec3 v_world_normal;
layout(location = 0) out vec4 FragColor;

void main() {
    vec3 encoded = normalize(v_world_normal) * 0.5 + 0.5;
    FragColor = vec4(encoded, 1.0);
}
