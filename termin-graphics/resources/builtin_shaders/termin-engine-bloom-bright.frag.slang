// Bloom bright pass — extracts pixels above a luminance threshold.
//
// Pass code reads the assigned numbers from shader layout metadata.

import termin_prelude;

struct BloomBrightParams {
    float2 texel_size;
    float threshold;
    float soft_threshold;
};

[[TerminScope("pass")]]
ConstantBuffer<BloomBrightParams> u_params;

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
    // The prefilter also performs the first 2x downsample. Nine bilinear taps
    // reproduce a separable [1 4 6 4 1] / 16 binomial kernel exactly and keep
    // point sources symmetric across the half-resolution sampling grid.
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
    float3 color = (a + c + g + i) * (25.0 / 256.0);
    color += (b + d + f + h) * (30.0 / 256.0);
    color += e * (36.0 / 256.0);

    float brightness = max(color.r, max(color.g, color.b));
    float knee = u_params.threshold * u_params.soft_threshold;
    float soft = brightness - u_params.threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, brightness - u_params.threshold) / max(brightness, 0.00001);
    contribution = max(contribution, 0.0);
    color = max(color, float3(0.0, 0.0, 0.0));

    FragmentOutput output;
    output.color = float4(color * contribution, 1.0);
    return output;
}
