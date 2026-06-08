#version 450 core
layout(location=0) in vec2 v_uv;

layout(std140, binding = 0) uniform GrayscaleParams {
    float u_strength;
};

layout(binding = 4) uniform sampler2D u_input;

layout(location=0) out vec4 FragColor;

void main() {
    vec3 color = texture(u_input, v_uv).rgb;
    float gray = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 result = mix(color, vec3(gray), u_strength);
    FragColor = vec4(result, 1.0);
}
