#version 450 core
layout(location = 0) in vec2 v_uv;

layout(std140, binding = 0) uniform BloomDownsampleParams {
    vec2 u_texel_size;
};

layout(binding = 4) uniform sampler2D u_texture;

layout(location = 0) out vec4 FragColor;

void main() {
    vec2 ts = u_texel_size;
    vec3 a = texture(u_texture, v_uv + vec2(-2.0, -2.0) * ts).rgb;
    vec3 b = texture(u_texture, v_uv + vec2( 0.0, -2.0) * ts).rgb;
    vec3 c = texture(u_texture, v_uv + vec2( 2.0, -2.0) * ts).rgb;
    vec3 d = texture(u_texture, v_uv + vec2(-2.0,  0.0) * ts).rgb;
    vec3 e = texture(u_texture, v_uv + vec2( 0.0,  0.0) * ts).rgb;
    vec3 f = texture(u_texture, v_uv + vec2( 2.0,  0.0) * ts).rgb;
    vec3 g = texture(u_texture, v_uv + vec2(-2.0,  2.0) * ts).rgb;
    vec3 h = texture(u_texture, v_uv + vec2( 0.0,  2.0) * ts).rgb;
    vec3 i = texture(u_texture, v_uv + vec2( 2.0,  2.0) * ts).rgb;
    vec3 j = texture(u_texture, v_uv + vec2(-1.0, -1.0) * ts).rgb;
    vec3 k = texture(u_texture, v_uv + vec2( 1.0, -1.0) * ts).rgb;
    vec3 l = texture(u_texture, v_uv + vec2(-1.0,  1.0) * ts).rgb;
    vec3 m = texture(u_texture, v_uv + vec2( 1.0,  1.0) * ts).rgb;
    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;
    FragColor = vec4(result, 1.0);
}
