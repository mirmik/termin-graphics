// Grayscale pass — desaturates the input image.
//

struct GrayscaleParams {
    float strength;
};

ConstantBuffer<GrayscaleParams> u_params;
Sampler2D u_input;

struct FragmentInput {
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    float3 color = u_input.Sample(input.uv).rgb;
    float gray = dot(color, float3(0.2126, 0.7152, 0.0722));
    float3 result = lerp(color, float3(gray, gray, gray), u_params.strength);

    FragmentOutput output;
    output.color = float4(result, 1.0);
    return output;
}
