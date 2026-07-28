// Bloom vertical blur — optimized five-sample, nine-tap-equivalent Gaussian.

import termin_prelude;

struct BloomBlurParams {
    float2 texel_size;
};

[[TerminScope("pass")]]
ConstantBuffer<BloomBlurParams> u_params;

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
    // Bilinear sampling combines adjacent pairs of the matching nine-tap
    // Gaussian kernel. This is the vertical filter used by Unity URP.
    float offset = u_params.texel_size.y;
    float3 result =
        u_texture.Sample(input.uv + float2(0.0, -3.23076923 * offset)).rgb *
        0.07027027;
    result +=
        u_texture.Sample(input.uv + float2(0.0, -1.38461538 * offset)).rgb *
        0.31621622;
    result += u_texture.Sample(input.uv).rgb * 0.22702703;
    result +=
        u_texture.Sample(input.uv + float2(0.0, 1.38461538 * offset)).rgb *
        0.31621622;
    result +=
        u_texture.Sample(input.uv + float2(0.0, 3.23076923 * offset)).rgb *
        0.07027027;

    FragmentOutput output;
    output.color = float4(result, 1.0);
    return output;
}
