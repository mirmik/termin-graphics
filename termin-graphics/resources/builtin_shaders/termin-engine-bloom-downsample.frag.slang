// Bloom horizontal downsample — optimized nine-tap-equivalent Gaussian.
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
    // Pass code supplies the sampling step in destination-pixel units. It is
    // one source texel for same-resolution filtering and two source texels
    // when this pass also downsamples.
    float offset = u_params.texel_size.x;
    // Bilinear sampling combines adjacent pairs of the same nine-tap kernel
    // used by the vertical pass, keeping both axes mathematically identical.
    float3 result =
        u_texture.Sample(input.uv + float2(-3.23076923 * offset, 0.0)).rgb *
        0.07027027;
    result +=
        u_texture.Sample(input.uv + float2(-1.38461538 * offset, 0.0)).rgb *
        0.31621622;
    result += u_texture.Sample(input.uv).rgb * 0.22702703;
    result +=
        u_texture.Sample(input.uv + float2(1.38461538 * offset, 0.0)).rgb *
        0.31621622;
    result +=
        u_texture.Sample(input.uv + float2(3.23076923 * offset, 0.0)).rgb *
        0.07027027;

    FragmentOutput output;
    output.color = float4(result, 1.0);
    return output;
}
