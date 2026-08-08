// Final linear display color to sRGB-encoded display output.

import termin_prelude;

[[TerminScope("transient")]]
Sampler2D u_input;

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

float linear_to_srgb(float value) {
    value = max(value, 0.0);
    if (value <= 0.0031308) {
        return 12.92 * value;
    }
    return 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    float4 linear = u_input.Sample(input.uv);
    float3 encoded = float3(
        linear_to_srgb(linear.r),
        linear_to_srgb(linear.g),
        linear_to_srgb(linear.b));

    FragmentOutput output;
    output.color = float4(clamp(encoded, 0.0, 1.0), clamp(linear.a, 0.0, 1.0));
    return output;
}
