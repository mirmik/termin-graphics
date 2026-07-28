// Bloom downsample pass — optimized 5x5 binomial filter for mip chain.
//

import termin_prelude;

struct BloomDownsampleParams {
    float2 texel_size;
};

[[TerminScope("pass")]]
ConstantBuffer<BloomDownsampleParams> u_params;

[[TerminScope("transient")]]
Sampler2D u_texture;

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    float2 offset = u_params.texel_size * 1.2;
    float3 a = u_texture.Sample(input.uv + float2(-offset.x, -offset.y)).rgb;
    float3 b = u_texture.Sample(input.uv + float2( 0.0,      -offset.y)).rgb;
    float3 c = u_texture.Sample(input.uv + float2( offset.x, -offset.y)).rgb;
    float3 d = u_texture.Sample(input.uv + float2(-offset.x,  0.0)).rgb;
    float3 e = u_texture.Sample(input.uv).rgb;
    float3 f = u_texture.Sample(input.uv + float2( offset.x,  0.0)).rgb;
    float3 g = u_texture.Sample(input.uv + float2(-offset.x,  offset.y)).rgb;
    float3 h = u_texture.Sample(input.uv + float2( 0.0,       offset.y)).rgb;
    float3 i = u_texture.Sample(input.uv + float2( offset.x,  offset.y)).rgb;
    float3 result = (a + c + g + i) * (25.0 / 256.0);
    result += (b + d + f + h) * (30.0 / 256.0);
    result += e * (36.0 / 256.0);

    FragmentOutput output;
    output.color = float4(result, 1.0);
    return output;
}
