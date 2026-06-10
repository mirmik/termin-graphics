// Depth-to-color pass — reads a depth texture, outputs grayscale color.
//

Sampler2D u_depth_tex;

struct FragmentInput {
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    float d = u_depth_tex.Sample(input.uv).r;

    FragmentOutput output;
    output.color = float4(float3(d, d, d), 1.0);
    return output;
}
