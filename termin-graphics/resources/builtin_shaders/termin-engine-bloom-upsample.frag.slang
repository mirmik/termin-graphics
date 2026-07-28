// Bloom upsample pass — smooth normalized high/low mip reconstruction.
//

import termin_prelude;

struct BloomUpsampleParams {
    float2 texel_size;
    float scatter;
};

[[TerminScope("pass")]]
ConstantBuffer<BloomUpsampleParams> u_params;

[[TerminScope("transient")]]
Sampler2D u_high;

[[TerminScope("transient")]]
Sampler2D u_low;

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    // Three bilinear taps per axis reproduce [1 4 6 4 1] / 16 while
    // retaining the cost of the old 3x3 tent.
    float2 offset = u_params.texel_size * 1.2;
    float3 a = u_low.Sample(input.uv + float2(-offset.x, -offset.y)).rgb;
    float3 b = u_low.Sample(input.uv + float2( 0.0,      -offset.y)).rgb;
    float3 c = u_low.Sample(input.uv + float2( offset.x, -offset.y)).rgb;
    float3 d = u_low.Sample(input.uv + float2(-offset.x,  0.0)).rgb;
    float3 e = u_low.Sample(input.uv).rgb;
    float3 f = u_low.Sample(input.uv + float2( offset.x,  0.0)).rgb;
    float3 g = u_low.Sample(input.uv + float2(-offset.x,  offset.y)).rgb;
    float3 h = u_low.Sample(input.uv + float2( 0.0,       offset.y)).rgb;
    float3 i = u_low.Sample(input.uv + float2( offset.x,  offset.y)).rgb;
    float3 upsampled = (a + c + g + i) * (25.0 / 256.0);
    upsampled += (b + d + f + h) * (30.0 / 256.0);
    upsampled += e * (36.0 / 256.0);

    FragmentOutput output;
    float3 high = u_high.Sample(input.uv).rgb;
    output.color = float4(lerp(high, upsampled, u_params.scatter), 1.0);
    return output;
}
