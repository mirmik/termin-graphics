#version 450 core
layout(location = 0) in vec2 v_uv;

layout(std140, binding = 0) uniform BloomBrightParams {
    float u_threshold;
    float u_soft_threshold;
};

layout(binding = 4) uniform sampler2D u_texture;

layout(location = 0) out vec4 FragColor;

void main() {
    vec3 color = texture(u_texture, v_uv).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee = u_threshold * u_soft_threshold;
    float soft = brightness - u_threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, brightness - u_threshold) / max(brightness, 0.00001);
    contribution = max(contribution, 0.0);
    FragColor = vec4(color * contribution, 1.0);
}
