#version 450 core
layout(location = 0) in vec2 v_uv;

layout(std140, binding = 0) uniform BloomCompositeParams {
    float u_intensity;
};

layout(binding = 4) uniform sampler2D u_original;
layout(binding = 5) uniform sampler2D u_bloom;

layout(location = 0) out vec4 FragColor;

void main() {
    vec3 original = texture(u_original, v_uv).rgb;
    vec3 bloom = texture(u_bloom, v_uv).rgb;
    vec3 result = original + bloom * u_intensity;
    FragColor = vec4(result, 1.0);
}
