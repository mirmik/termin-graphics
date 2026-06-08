#version 450 core
layout(location=0) in vec2 v_uv;

layout(std140, binding = 0) uniform TonemapParams {
    float u_exposure;
    int u_method;
};

layout(binding = 4) uniform sampler2D u_input;

layout(location=0) out vec4 FragColor;

vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 reinhard_tonemap(vec3 x) {
    return x / (x + vec3(1.0));
}

void main() {
    vec3 color = texture(u_input, v_uv).rgb;
    color *= u_exposure;
    if (u_method == 0) {
        color = aces_tonemap(color);
    } else if (u_method == 1) {
        color = reinhard_tonemap(color);
    }
    FragColor = vec4(color, 1.0);
}
