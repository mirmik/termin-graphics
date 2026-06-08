#version 450 core
layout(location = 0) in vec2 v_uv;

layout(std140, binding = 0) uniform BloomUpsampleParams {
    vec2 u_texel_size;
    float u_blend_factor;
};

layout(binding = 4) uniform sampler2D u_texture;

layout(location = 0) out vec4 FragColor;

void main() {
    vec2 ts = u_texel_size;
    vec3 a = texture(u_texture, v_uv + vec2(-1.0, -1.0) * ts).rgb;
    vec3 b = texture(u_texture, v_uv + vec2( 0.0, -1.0) * ts).rgb;
    vec3 c = texture(u_texture, v_uv + vec2( 1.0, -1.0) * ts).rgb;
    vec3 d = texture(u_texture, v_uv + vec2(-1.0,  0.0) * ts).rgb;
    vec3 e = texture(u_texture, v_uv + vec2( 0.0,  0.0) * ts).rgb;
    vec3 f = texture(u_texture, v_uv + vec2( 1.0,  0.0) * ts).rgb;
    vec3 g = texture(u_texture, v_uv + vec2(-1.0,  1.0) * ts).rgb;
    vec3 h = texture(u_texture, v_uv + vec2( 0.0,  1.0) * ts).rgb;
    vec3 i = texture(u_texture, v_uv + vec2( 1.0,  1.0) * ts).rgb;
    vec3 upsampled = e * 4.0;
    upsampled += (b + d + f + h) * 2.0;
    upsampled += (a + c + g + i);
    upsampled /= 16.0;
    FragColor = vec4(upsampled * u_blend_factor, 1.0);
}
